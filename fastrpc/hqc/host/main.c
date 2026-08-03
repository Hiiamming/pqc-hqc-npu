#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hqc.h"
#include "parameters.h"
#include "rpcmem.h"

#if HQC_PARAM_LEVEL == 128
#include "hqc1_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC1_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc1_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc1_fixture_expected_messages
#elif HQC_PARAM_LEVEL == 192
#include "hqc3_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC3_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc3_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc3_fixture_expected_messages
#elif HQC_PARAM_LEVEL == 256
#include "hqc5_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC5_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc5_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc5_fixture_expected_messages
#else
#error "HQC_PARAM_LEVEL must be 128, 192, or 256"
#endif

#ifndef HQC_PARAM_LEVEL
#define HQC_PARAM_LEVEL 128
#endif

#ifndef HQC_DEFAULT_BENCH_ITERS
#define HQC_DEFAULT_BENCH_ITERS 1000
#endif

#define HQC_BUFFER_MODE_DIRECT 0
#define HQC_BUFFER_MODE_COPY 1
#define HQC_BUFFER_MODE_L2FETCH 2
#define HQC_BUFFER_MODE_VTCM 3

#define HQC_BUFFER_STATUS_OK 0
#define HQC_BUFFER_STATUS_BAD_ARGS -1
#define HQC_BUFFER_STATUS_UNSUPPORTED -2

static size_t align_up_size(size_t value, size_t alignment)
{
    return (value + alignment - 1u) & ~(alignment - 1u);
}

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int parse_positive_int(const char *arg, int *value)
{
    char *end = NULL;
    long parsed;

    if (arg == NULL || *arg == '\0') {
        return 0;
    }

    parsed = strtol(arg, &end, 10);
    if (*end != '\0' || parsed <= 0 || parsed > 2147483647L) {
        return 0;
    }

    *value = (int)parsed;
    return 1;
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage:\n"
            "  %s [iters]\n"
            "  %s bench [iters]\n"
            "  %s paper-batch <decode-count>\n"
            "  %s ping [calls]\n"
            "  %s decode-one [calls]\n"
            "  %s substage <1..9> [iters]\n"
            "  %s open-close [calls]\n"
            "  %s payload-in <malloc|rpcmem-cached|rpcmem-uncached> <bytes> [calls]\n"
            "  %s payload-out <malloc|rpcmem-cached|rpcmem-uncached> <bytes> [calls]\n"
            "  %s payload-inout <malloc|rpcmem-cached|rpcmem-uncached> <in-bytes> <out-bytes> [calls]\n"
            "  %s buffer-bench <malloc|rpcmem-cached|rpcmem-uncached> <direct|copy|l2fetch|vtcm> [iters] [codeword-count] [rpc-calls]\n",
            prog,
            prog,
            prog,
            prog,
            prog,
            prog,
            prog,
            prog,
            prog,
            prog,
            prog);
}

static int parse_alloc_mode(const char *arg)
{
    if (strcmp(arg, "malloc") == 0) {
        return 0;
    }
    if (strcmp(arg, "rpcmem-cached") == 0) {
        return 1;
    }
    if (strcmp(arg, "rpcmem-uncached") == 0) {
        return 2;
    }
    return -1;
}

static int parse_dsp_mode(const char *arg)
{
    if (strcmp(arg, "direct") == 0) {
        return HQC_BUFFER_MODE_DIRECT;
    }
    if (strcmp(arg, "copy") == 0) {
        return HQC_BUFFER_MODE_COPY;
    }
    if (strcmp(arg, "l2fetch") == 0) {
        return HQC_BUFFER_MODE_L2FETCH;
    }
    if (strcmp(arg, "vtcm") == 0) {
        return HQC_BUFFER_MODE_VTCM;
    }
    return -1;
}

static const char *buffer_status_name(int status)
{
    switch (status) {
        case HQC_BUFFER_STATUS_OK:
            return "OK";
        case HQC_BUFFER_STATUS_BAD_ARGS:
            return "BAD_ARGS";
        case HQC_BUFFER_STATUS_UNSUPPORTED:
            return "SKIP";
        default:
            return "UNKNOWN";
    }
}

static void *alloc_buffer(int alloc_mode, size_t size)
{
    void *ptr = NULL;

    if (alloc_mode == 0) {
        if (posix_memalign(&ptr, 128, size) != 0) {
            return NULL;
        }
        memset(ptr, 0, size);
        return ptr;
    }

    int flags = alloc_mode == 1
                    ? (RPCMEM_FLAG_CACHED | RPCMEM_TRY_MAP_STATIC)
                    : RPCMEM_FLAG_UNCACHED;
    ptr = rpcmem_alloc(RPCMEM_HEAP_ID_SYSTEM, flags, (int)size);
    if (ptr != NULL) {
        memset(ptr, 0, size);
    }
    return ptr;
}

static void free_buffer(int alloc_mode, void *ptr)
{
    if (ptr == NULL) {
        return;
    }
    if (alloc_mode == 0) {
        free(ptr);
    } else {
        rpcmem_free(ptr);
    }
}

static void configure_fastrpc(void)
{
    struct remote_rpc_control_unsigned_module unsigned_module = {
        .domain = CDSP_DOMAIN_ID,
        .enable = 1,
    };
    int ret = remote_session_control(DSPRPC_CONTROL_UNSIGNED_MODULE,
                                     &unsigned_module,
                                     sizeof(unsigned_module));
    if (ret != 0) {
        printf("[fastrpc-intrinsic-decode] warning: unsigned module enable failed, error=0x%x\n", ret);
    }

}

static int open_hqc(remote_handle64 *handle)
{
    int ret;

    configure_fastrpc();

    ret = hqc_open(hqc_URI CDSP_DOMAIN, handle);
    if (ret != 0) {
        printf("[fastrpc-intrinsic-decode] hqc_open failed, error=0x%x\n", ret);
        return ret;
    }

    return 0;
}

static int close_hqc(remote_handle64 handle)
{
    int ret = hqc_close(handle);
    if (ret != 0) {
        printf("[fastrpc-intrinsic-decode] warning: hqc_close failed, error=0x%x\n", ret);
    }
    return ret;
}

static int report_rpc_error(int ret)
{
    unsigned error = (unsigned)ret;

    printf("[fastrpc-intrinsic-decode] FastRPC error=0x%x\n", ret);
    if (error == 0x80000406u) {
        printf("[fastrpc-intrinsic-decode] hint: libhqc_skel.so not found; check ADSP_LIBRARY_PATH\n");
    } else if (error == 0x80000403u) {
        printf("[fastrpc-intrinsic-decode] hint: signature/load rejection; check testsig file\n");
    }
    return 1;
}

static int run_bench(int iters)
{
    remote_handle64 handle = 0;
    int total_decodes = 0;
    int total_rs_errors = 0;
    int checksum = 0;
    int passed = 0;
    int ret;

    printf("[fastrpc-intrinsic-decode] calling HQC-%d cDSP HVX intrinsic decoder, iters=%d\n",
           HQC_PARAM_LEVEL,
           iters);

    ret = open_hqc(&handle);
    if (ret != 0) {
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    ret = hqc_decode_bench(handle, iters, &total_decodes, &total_rs_errors, &checksum, &passed);
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = total_decodes ? (double)elapsed_ns / (double)total_decodes : 0.0;

    printf("[fastrpc-intrinsic-decode] total_decodes=%d total_rs_symbol_errors=%d checksum=0x%08x result=%s\n",
           total_decodes,
           total_rs_errors,
           (unsigned)checksum,
           passed ? "PASS" : "FAIL");
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0);

    return passed ? 0 : 1;
}

static int run_paper_batch(int decode_count)
{
    remote_handle64 handle = 0;
    int total_decodes = 0;
    int total_rs_errors = 0;
    int checksum = 0;
    int passed = 0;
    int ret;

    printf("[fastrpc-intrinsic-decode] paper-batch HQC-%d cDSP HVX intrinsic decoder, decode_count=%d\n",
           HQC_PARAM_LEVEL,
           decode_count);

    ret = open_hqc(&handle);
    if (ret != 0) {
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    ret = hqc_decode_bench_count(handle,
                                 decode_count,
                                 &total_decodes,
                                 &total_rs_errors,
                                 &checksum,
                                 &passed);
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = total_decodes ? (double)elapsed_ns / (double)total_decodes : 0.0;

    printf("[fastrpc-intrinsic-decode] mode=paper-batch total_decodes=%d total_rs_symbol_errors=%d checksum=0x%08x result=%s\n",
           total_decodes,
           total_rs_errors,
           (unsigned)checksum,
           passed ? "PASS" : "FAIL");
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0);

    return passed ? 0 : 1;
}

static int run_ping(int calls)
{
    remote_handle64 handle = 0;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    int checksum = 0;
    int ret;

    printf("[fastrpc-intrinsic-decode] ping HQC-%d cDSP, calls=%d\n",
           HQC_PARAM_LEVEL,
           calls);

    ret = open_hqc(&handle);
    if (ret != 0) {
        return 1;
    }

    start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        int one_checksum = 0;
        ret = hqc_ping(handle, 1, &one_checksum);
        if (ret != 0) {
            break;
        }
        checksum ^= one_checksum ^ i;
    }
    elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_rpc = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=ping total_calls=%d checksum=0x%08x result=PASS\n",
           calls,
           (unsigned)checksum);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_rpc=%.1f us_per_rpc=%.3f\n",
           elapsed_ms,
           ns_per_rpc,
           ns_per_rpc / 1000.0);

    return 0;
}

static int run_decode_one(int calls)
{
    remote_handle64 handle = 0;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    int total_rs_errors = 0;
    int checksum = 0;
    int passed = 1;
    int ret;

    printf("[fastrpc-intrinsic-decode] decode-one HQC-%d cDSP, calls=%d\n",
           HQC_PARAM_LEVEL,
           calls);

    ret = open_hqc(&handle);
    if (ret != 0) {
        return 1;
    }

    start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        int rs_errors = 0;
        int one_checksum = 0;
        int one_passed = 0;

        ret = hqc_decode_one(handle, &rs_errors, &one_checksum, &one_passed);
        if (ret != 0) {
            break;
        }

        total_rs_errors += rs_errors;
        checksum ^= one_checksum;
        passed &= one_passed;
    }
    elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=decode-one total_decodes=%d total_rs_symbol_errors=%d checksum=0x%08x result=%s\n",
           calls,
           total_rs_errors,
           (unsigned)checksum,
           passed ? "PASS" : "FAIL");
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f us_per_rpc_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0,
           ns_per_decode / 1000.0);

    return passed ? 0 : 1;
}

static const char *substage_name(int stage)
{
    switch (stage) {
        case 1: return "rm_expand";
        case 2: return "rm_hadamard";
        case 3: return "rm_peak";
        case 4: return "rs_syndrome";
        case 5: return "rs_elp";
        case 6: return "rs_roots";
        case 7: return "rs_z";
        case 8: return "rs_error_values";
        case 9: return "rs_correct";
        default: return "unknown";
    }
}

static int run_substage(int stage, int iters)
{
    remote_handle64 handle = 0;
    uint64_t start_ns;
    uint64_t elapsed_ns;
    int total_ops = 0;
    int checksum = 0;
    int passed = 0;
    int ret;

    if (stage < 1 || stage > 9 || iters <= 0) {
        usage("hqc_host");
        return 2;
    }

    printf("[fastrpc-intrinsic-decode] substage HQC-%d stage=%s(%d) iters=%d\n",
           HQC_PARAM_LEVEL,
           substage_name(stage),
           stage,
           iters);

    ret = open_hqc(&handle);
    if (ret != 0) {
        return 1;
    }

    start_ns = monotonic_ns();
    ret = hqc_substage_bench(handle, stage, iters, &total_ops, &checksum, &passed);
    elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_op = total_ops ? (double)elapsed_ns / (double)total_ops : 0.0;

    printf("[fastrpc-intrinsic-decode] mode=substage stage=%s stage_id=%d total_ops=%d checksum=0x%08x result=%s\n",
           substage_name(stage),
           stage,
           total_ops,
           (unsigned)checksum,
           passed ? "PASS" : "FAIL");
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_op=%.1f us_per_op=%.3f\n",
           elapsed_ms,
           ns_per_op,
           ns_per_op / 1000.0);

    return passed ? 0 : 1;
}

static int run_open_close(int calls)
{
    uint64_t start_ns;
    uint64_t elapsed_ns;
    int ret = 0;

    printf("[fastrpc-intrinsic-decode] open-close HQC-%d cDSP, calls=%d\n",
           HQC_PARAM_LEVEL,
           calls);

    start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        remote_handle64 handle = 0;
        ret = open_hqc(&handle);
        if (ret != 0) {
            break;
        }
        ret = close_hqc(handle);
        if (ret != 0) {
            break;
        }
    }
    elapsed_ns = monotonic_ns() - start_ns;

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_rpc = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=open-close total_calls=%d result=PASS\n",
           calls);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_rpc=%.1f us_per_rpc=%.3f\n",
           elapsed_ms,
           ns_per_rpc,
           ns_per_rpc / 1000.0);

    return 0;
}

static void fill_payload(unsigned char *buf, size_t size)
{
    uint32_t state = 0x12345678u ^ (uint32_t)size;

    for (size_t i = 0; i < size; ++i) {
        state = (state * 1664525u) + 1013904223u;
        buf[i] = (unsigned char)(state >> 24);
    }
}

static int run_payload_in(const char *alloc_name, int bytes, int calls)
{
    remote_handle64 handle = 0;
    int alloc_mode = parse_alloc_mode(alloc_name);
    unsigned char *input = NULL;
    int checksum = 0;
    int ret;

    if (alloc_mode < 0 || bytes <= 0 || calls <= 0) {
        usage("hqc_host");
        return 2;
    }

    input = alloc_buffer(alloc_mode, (size_t)bytes);
    if (input == NULL) {
        fprintf(stderr, "[fastrpc-intrinsic-decode] payload input allocation failed: alloc=%s bytes=%d\n",
                alloc_name,
                bytes);
        return 1;
    }
    fill_payload(input, (size_t)bytes);

    printf("[fastrpc-intrinsic-decode] payload-in HQC-%d alloc=%s bytes=%d calls=%d\n",
           HQC_PARAM_LEVEL,
           alloc_name,
           bytes,
           calls);

    ret = open_hqc(&handle);
    if (ret != 0) {
        free_buffer(alloc_mode, input);
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        int one_checksum = 0;
        ret = hqc_payload_in(handle, input, bytes, 1, &one_checksum);
        if (ret != 0) {
            break;
        }
        checksum ^= one_checksum ^ i;
    }
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);
    free_buffer(alloc_mode, input);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_rpc = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=payload-in alloc=%s bytes=%d total_calls=%d checksum=0x%08x result=PASS\n",
           alloc_name,
           bytes,
           calls,
           (unsigned)checksum);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_rpc=%.1f us_per_rpc=%.3f\n",
           elapsed_ms,
           ns_per_rpc,
           ns_per_rpc / 1000.0);

    return 0;
}

static int run_payload_out(const char *alloc_name, int bytes, int calls)
{
    remote_handle64 handle = 0;
    int alloc_mode = parse_alloc_mode(alloc_name);
    unsigned char *output = NULL;
    int checksum = 0;
    int host_checksum = 0;
    int ret;

    if (alloc_mode < 0 || bytes <= 0 || calls <= 0) {
        usage("hqc_host");
        return 2;
    }

    output = alloc_buffer(alloc_mode, (size_t)bytes);
    if (output == NULL) {
        fprintf(stderr, "[fastrpc-intrinsic-decode] payload output allocation failed: alloc=%s bytes=%d\n",
                alloc_name,
                bytes);
        return 1;
    }

    printf("[fastrpc-intrinsic-decode] payload-out HQC-%d alloc=%s bytes=%d calls=%d\n",
           HQC_PARAM_LEVEL,
           alloc_name,
           bytes,
           calls);

    ret = open_hqc(&handle);
    if (ret != 0) {
        free_buffer(alloc_mode, output);
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        int one_checksum = 0;
        ret = hqc_payload_out(handle, output, bytes, 1, &one_checksum);
        if (ret != 0) {
            break;
        }
        checksum ^= one_checksum ^ i;
    }
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    for (int i = 0; i < bytes; ++i) {
        host_checksum = (host_checksum << 5) ^ (host_checksum >> 2) ^ output[i] ^ i;
    }
    free_buffer(alloc_mode, output);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_rpc = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=payload-out alloc=%s bytes=%d total_calls=%d checksum=0x%08x host_checksum=0x%08x result=PASS\n",
           alloc_name,
           bytes,
           calls,
           (unsigned)checksum,
           (unsigned)host_checksum);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_rpc=%.1f us_per_rpc=%.3f\n",
           elapsed_ms,
           ns_per_rpc,
           ns_per_rpc / 1000.0);

    return 0;
}

static int run_payload_inout(const char *alloc_name, int in_bytes, int out_bytes, int calls)
{
    remote_handle64 handle = 0;
    int alloc_mode = parse_alloc_mode(alloc_name);
    unsigned char *input = NULL;
    unsigned char *output = NULL;
    int checksum = 0;
    int host_checksum = 0;
    int ret;

    if (alloc_mode < 0 || in_bytes <= 0 || out_bytes <= 0 || calls <= 0) {
        usage("hqc_host");
        return 2;
    }

    input = alloc_buffer(alloc_mode, (size_t)in_bytes);
    output = alloc_buffer(alloc_mode, (size_t)out_bytes);
    if (input == NULL || output == NULL) {
        fprintf(stderr, "[fastrpc-intrinsic-decode] payload inout allocation failed: alloc=%s in=%d out=%d\n",
                alloc_name,
                in_bytes,
                out_bytes);
        free_buffer(alloc_mode, input);
        free_buffer(alloc_mode, output);
        return 1;
    }
    fill_payload(input, (size_t)in_bytes);

    printf("[fastrpc-intrinsic-decode] payload-inout HQC-%d alloc=%s in_bytes=%d out_bytes=%d calls=%d\n",
           HQC_PARAM_LEVEL,
           alloc_name,
           in_bytes,
           out_bytes,
           calls);

    ret = open_hqc(&handle);
    if (ret != 0) {
        free_buffer(alloc_mode, input);
        free_buffer(alloc_mode, output);
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    for (int i = 0; i < calls; ++i) {
        int one_checksum = 0;
        ret = hqc_payload_inout(handle, input, in_bytes, output, out_bytes, 1, &one_checksum);
        if (ret != 0) {
            break;
        }
        checksum ^= one_checksum ^ i;
    }
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    for (int i = 0; i < out_bytes; ++i) {
        host_checksum = (host_checksum << 5) ^ (host_checksum >> 2) ^ output[i] ^ i;
    }
    free_buffer(alloc_mode, input);
    free_buffer(alloc_mode, output);

    if (ret != 0) {
        return report_rpc_error(ret);
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_rpc = (double)elapsed_ns / (double)calls;

    printf("[fastrpc-intrinsic-decode] mode=payload-inout alloc=%s in_bytes=%d out_bytes=%d total_calls=%d checksum=0x%08x host_checksum=0x%08x result=PASS\n",
           alloc_name,
           in_bytes,
           out_bytes,
           calls,
           (unsigned)checksum,
           (unsigned)host_checksum);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_rpc=%.1f us_per_rpc=%.3f\n",
           elapsed_ms,
           ns_per_rpc,
           ns_per_rpc / 1000.0);

    return 0;
}

static int run_buffer_bench(const char *alloc_name,
                            const char *dsp_name,
                            int iters,
                            int codeword_count,
                            int rpc_calls)
{
    remote_handle64 handle = 0;
    int alloc_mode = parse_alloc_mode(alloc_name);
    int dsp_mode = parse_dsp_mode(dsp_name);
    int total_decodes = 0;
    int checksum = 0;
    int passed = 0;
    int mode_status = 0;
    int host_ok = 1;
    int ret;

    if (alloc_mode < 0 || dsp_mode < 0 || iters <= 0 || codeword_count <= 0 || rpc_calls <= 0) {
        usage("hqc_host");
        return 2;
    }

    size_t codeword_stride = align_up_size(VEC_N1N2_SIZE_BYTES, 128);
    size_t message_stride = align_up_size(PARAM_K, 128);
    size_t codewords_size = codeword_stride * (size_t)codeword_count;
    size_t messages_size = message_stride * (size_t)codeword_count;

    unsigned char *codewords = alloc_buffer(alloc_mode, codewords_size);
    unsigned char *messages = alloc_buffer(alloc_mode, messages_size);
    if (codewords == NULL || messages == NULL) {
        fprintf(stderr, "[fastrpc-intrinsic-decode] buffer allocation failed: alloc=%s codewords=%zu messages=%zu\n",
                alloc_name,
                codewords_size,
                messages_size);
        free_buffer(alloc_mode, codewords);
        free_buffer(alloc_mode, messages);
        return 1;
    }

    for (int i = 0; i < codeword_count; ++i) {
        size_t fixture = (size_t)i % HQC_FIXTURE_COUNT;
        memcpy(codewords + (size_t)i * codeword_stride,
               HQC_FIXTURE_CODEWORDS[fixture],
               VEC_N1N2_SIZE_BYTES);
    }

    printf("[fastrpc-intrinsic-decode] buffer-bench HQC-%d alloc=%s dsp_mode=%s iters=%d codeword_count=%d rpc_calls=%d codeword_stride=%zu message_stride=%zu\n",
           HQC_PARAM_LEVEL,
           alloc_name,
           dsp_name,
           iters,
           codeword_count,
           rpc_calls,
           codeword_stride,
           message_stride);

    ret = open_hqc(&handle);
    if (ret != 0) {
        free_buffer(alloc_mode, codewords);
        free_buffer(alloc_mode, messages);
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    passed = 1;
    for (int call = 0; call < rpc_calls; ++call) {
        int one_decodes = 0;
        int one_checksum = 0;
        int one_passed = 0;
        int one_status = 0;

        ret = hqc_decode_buffer_bench(handle,
                                      codewords,
                                      (int)codewords_size,
                                      messages,
                                      (int)messages_size,
                                      iters,
                                      codeword_count,
                                      (int)codeword_stride,
                                      (int)message_stride,
                                      dsp_mode,
                                      &one_decodes,
                                      &one_checksum,
                                      &one_passed,
                                      &one_status);
        if (ret != 0) {
            break;
        }

        total_decodes += one_decodes;
        checksum ^= one_checksum ^ call;
        passed &= one_passed;
        mode_status = one_status;
        if (mode_status != HQC_BUFFER_STATUS_OK) {
            break;
        }
    }
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    close_hqc(handle);

    if (ret != 0) {
        free_buffer(alloc_mode, codewords);
        free_buffer(alloc_mode, messages);
        return report_rpc_error(ret);
    }

    if (mode_status == HQC_BUFFER_STATUS_OK) {
        for (int i = 0; i < codeword_count; ++i) {
            size_t fixture = (size_t)i % HQC_FIXTURE_COUNT;
            host_ok &= memcmp(messages + (size_t)i * message_stride,
                              HQC_FIXTURE_EXPECTED_MESSAGES[fixture],
                              PARAM_K) == 0;
        }
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = total_decodes ? (double)elapsed_ns / (double)total_decodes : 0.0;
    const char *status_name = buffer_status_name(mode_status);
    const char *result_name = (mode_status == HQC_BUFFER_STATUS_UNSUPPORTED)
                                  ? "SKIP"
                                  : ((passed && host_ok) ? "PASS" : "FAIL");

    printf("[fastrpc-intrinsic-decode] mode=buffer-bench alloc=%s dsp_mode=%s mode_status=%s rpc_calls=%d total_decodes=%d checksum=0x%08x result=%s\n",
           alloc_name,
           dsp_name,
           status_name,
           rpc_calls,
           total_decodes,
           (unsigned)checksum,
           result_name);
    printf("[fastrpc-intrinsic-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0);

    free_buffer(alloc_mode, codewords);
    free_buffer(alloc_mode, messages);

    if (mode_status == HQC_BUFFER_STATUS_UNSUPPORTED) {
        return 0;
    }
    return (passed && host_ok) ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *mode = "bench";
    int count = HQC_DEFAULT_BENCH_ITERS;

    if (argc == 2) {
        if (!parse_positive_int(argv[1], &count)) {
            mode = argv[1];
            count = HQC_DEFAULT_BENCH_ITERS;
        }
    } else if (argc == 3) {
        if (strcmp(argv[1], "substage") == 0) {
            int stage = 0;
            if (!parse_positive_int(argv[2], &stage)) {
                fprintf(stderr, "Invalid substage: %s\n", argv[2]);
                usage(argv[0]);
                return 2;
            }
            return run_substage(stage, HQC_DEFAULT_BENCH_ITERS);
        }
        if (strcmp(argv[1], "paper-batch") == 0) {
            int decode_count = 0;
            if (!parse_positive_int(argv[2], &decode_count)) {
                fprintf(stderr, "Invalid decode-count: %s\n", argv[2]);
                usage(argv[0]);
                return 2;
            }
            return run_paper_batch(decode_count);
        }
        mode = argv[1];
        if (!parse_positive_int(argv[2], &count)) {
            fprintf(stderr, "Invalid count: %s\n", argv[2]);
            usage(argv[0]);
            return 2;
        }
    } else if (argc > 3) {
        if (strcmp(argv[1], "payload-in") == 0 ||
            strcmp(argv[1], "payload-out") == 0) {
            int bytes = 0;
            int calls = 100000;

            if (argc != 4 && argc != 5) {
                usage(argv[0]);
                return 2;
            }
            if (!parse_positive_int(argv[3], &bytes)) {
                fprintf(stderr, "Invalid bytes: %s\n", argv[3]);
                usage(argv[0]);
                return 2;
            }
            if (argc == 5 && !parse_positive_int(argv[4], &calls)) {
                fprintf(stderr, "Invalid calls: %s\n", argv[4]);
                usage(argv[0]);
                return 2;
            }
            if (strcmp(argv[1], "payload-in") == 0) {
                return run_payload_in(argv[2], bytes, calls);
            }
            return run_payload_out(argv[2], bytes, calls);
        }
        if (strcmp(argv[1], "payload-inout") == 0) {
            int in_bytes = 0;
            int out_bytes = 0;
            int calls = 100000;

            if (argc != 5 && argc != 6) {
                usage(argv[0]);
                return 2;
            }
            if (!parse_positive_int(argv[3], &in_bytes)) {
                fprintf(stderr, "Invalid in-bytes: %s\n", argv[3]);
                usage(argv[0]);
                return 2;
            }
            if (!parse_positive_int(argv[4], &out_bytes)) {
                fprintf(stderr, "Invalid out-bytes: %s\n", argv[4]);
                usage(argv[0]);
                return 2;
            }
            if (argc == 6 && !parse_positive_int(argv[5], &calls)) {
                fprintf(stderr, "Invalid calls: %s\n", argv[5]);
                usage(argv[0]);
                return 2;
            }
            return run_payload_inout(argv[2], in_bytes, out_bytes, calls);
        }
        if (strcmp(argv[1], "buffer-bench") == 0) {
            int iters = HQC_DEFAULT_BENCH_ITERS;
            int codeword_count = 16;
            int rpc_calls = 1;

            if (argc != 4 && argc != 5 && argc != 6 && argc != 7) {
                usage(argv[0]);
                return 2;
            }
            if (argc >= 5 && !parse_positive_int(argv[4], &iters)) {
                fprintf(stderr, "Invalid iters: %s\n", argv[4]);
                usage(argv[0]);
                return 2;
            }
            if (argc >= 6 && !parse_positive_int(argv[5], &codeword_count)) {
                fprintf(stderr, "Invalid codeword-count: %s\n", argv[5]);
                usage(argv[0]);
                return 2;
            }
            if (argc >= 7 && !parse_positive_int(argv[6], &rpc_calls)) {
                fprintf(stderr, "Invalid rpc-calls: %s\n", argv[6]);
                usage(argv[0]);
                return 2;
            }
            return run_buffer_bench(argv[2], argv[3], iters, codeword_count, rpc_calls);
        }
        if (strcmp(argv[1], "substage") == 0) {
            int stage = 0;
            int iters = HQC_DEFAULT_BENCH_ITERS;

            if (argc != 3 && argc != 4) {
                usage(argv[0]);
                return 2;
            }
            if (!parse_positive_int(argv[2], &stage)) {
                fprintf(stderr, "Invalid substage: %s\n", argv[2]);
                usage(argv[0]);
                return 2;
            }
            if (argc == 4 && !parse_positive_int(argv[3], &iters)) {
                fprintf(stderr, "Invalid iters: %s\n", argv[3]);
                usage(argv[0]);
                return 2;
            }
            return run_substage(stage, iters);
        }
        usage(argv[0]);
        return 2;
    }

    if (strcmp(mode, "bench") == 0) {
        return run_bench(count);
    }
    if (strcmp(mode, "ping") == 0) {
        return run_ping(count);
    }
    if (strcmp(mode, "decode-one") == 0) {
        return run_decode_one(count);
    }
    if (strcmp(mode, "substage") == 0) {
        usage(argv[0]);
        return 2;
    }
    if (strcmp(mode, "open-close") == 0) {
        return run_open_close(count);
    }

    fprintf(stderr, "Unknown mode: %s\n", mode);
    usage(argv[0]);
    return 2;
}
