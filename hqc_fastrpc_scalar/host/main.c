#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "hqc.h"

#ifndef HQC128_DEFAULT_BENCH_ITERS
#define HQC128_DEFAULT_BENCH_ITERS 1000
#endif

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
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
        printf("[fastrpc-scalar-decode] warning: unsigned module enable failed, error=0x%x\n", ret);
    }
}

int main(int argc, char **argv)
{
    int iters = HQC128_DEFAULT_BENCH_ITERS;
    int total_decodes = 0;
    int total_rs_errors = 0;
    int checksum = 0;
    int passed = 0;

    if (argc > 1) {
        iters = atoi(argv[1]);
        if (iters <= 0) {
            fprintf(stderr, "Invalid iters: %s\n", argv[1]);
            return 2;
        }
    }

    printf("[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=%d\n", iters);
    configure_fastrpc();

    remote_handle64 handle = 0;
    int ret = hqc_open(hqc_URI CDSP_DOMAIN, &handle);
    if (ret != 0) {
        printf("[fastrpc-scalar-decode] hqc_open failed, error=0x%x\n", ret);
        return 1;
    }

    uint64_t start_ns = monotonic_ns();
    ret = hqc_decode_bench(handle, iters, &total_decodes, &total_rs_errors, &checksum, &passed);
    uint64_t elapsed_ns = monotonic_ns() - start_ns;
    int close_ret = hqc_close(handle);
    if (close_ret != 0) {
        printf("[fastrpc-scalar-decode] warning: hqc_close failed, error=0x%x\n", close_ret);
    }

    if (ret != 0) {
        unsigned error = (unsigned)ret;
        printf("[fastrpc-scalar-decode] FastRPC error=0x%x\n", ret);
        if (error == 0x80000406u) {
            printf("[fastrpc-scalar-decode] hint: libhqc_skel.so not found; check ADSP_LIBRARY_PATH\n");
        } else if (error == 0x80000403u) {
            printf("[fastrpc-scalar-decode] hint: signature/load rejection; check testsig file\n");
        }
        return 1;
    }

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = total_decodes ? (double)elapsed_ns / (double)total_decodes : 0.0;

    printf("[fastrpc-scalar-decode] total_decodes=%d total_rs_symbol_errors=%d checksum=0x%08x result=%s\n",
           total_decodes,
           total_rs_errors,
           (unsigned)checksum,
           passed ? "PASS" : "FAIL");
    printf("[fastrpc-scalar-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0);

    return passed ? 0 : 1;
}
