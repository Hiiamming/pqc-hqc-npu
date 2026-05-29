#include <stdint.h>
#include <string.h>

#include <hexagon_protos.h>

#include "hqc.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#if HQC_USE_WORKER_POOL
#include "worker_pool.h"
#endif

#ifndef HQC_PARAM_LEVEL
#define HQC_PARAM_LEVEL 128
#endif

#ifndef HQC_USE_WORKER_POOL
#define HQC_USE_WORKER_POOL 0
#endif

#if HQC_PARAM_LEVEL == 128
#include "hqc1_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC1_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc1_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc1_fixture_expected_messages
#define HQC_FIXTURE_RS_SYMBOL_ERRORS hqc1_fixture_rs_symbol_errors
#elif HQC_PARAM_LEVEL == 192
#include "hqc3_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC3_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc3_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc3_fixture_expected_messages
#define HQC_FIXTURE_RS_SYMBOL_ERRORS hqc3_fixture_rs_symbol_errors
#elif HQC_PARAM_LEVEL == 256
#include "hqc5_decode_fixture.h"
#define HQC_FIXTURE_COUNT HQC5_FIXTURE_COUNT
#define HQC_FIXTURE_CODEWORDS hqc5_fixture_codewords
#define HQC_FIXTURE_EXPECTED_MESSAGES hqc5_fixture_expected_messages
#define HQC_FIXTURE_RS_SYMBOL_ERRORS hqc5_fixture_rs_symbol_errors
#else
#error "HQC_PARAM_LEVEL must be 128, 192, or 256"
#endif

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

#define HQC_BUFFER_MODE_DIRECT 0
#define HQC_BUFFER_MODE_COPY 1
#define HQC_BUFFER_MODE_L2FETCH 2
#define HQC_BUFFER_MODE_VTCM 3
#define HQC_BUFFER_MODE_WORKER_POOL 4

#define HQC_BUFFER_STATUS_OK 0
#define HQC_BUFFER_STATUS_BAD_ARGS -1
#define HQC_BUFFER_STATUS_UNSUPPORTED -2

#if HQC_USE_WORKER_POOL
typedef struct {
    const unsigned char *codewords;
    unsigned char *messages;
    int codeword_stride;
    int message_stride;
    int start;
    int end;
    int decodes;
    int ok;
    uint32_t checksum;
    worker_synctoken_t *token;
} hqc_worker_decode_job_t;
#endif

static size_t decode_one_fixture_index = 0;

int hqc_open(const char *uri, remote_handle64 *handle)
{
    (void)uri;
    *handle = 1;
    return 0;
}

int hqc_close(remote_handle64 handle)
{
    (void)handle;
    return 0;
}

static void hqc_decode_direct(uint8_t message[PARAM_K], const uint64_t codeword[VEC_N1N2_SIZE_64])
{
    uint64_t rs_words[VEC_N1_SIZE_64] = {0};
    uint64_t message_words[MSG_WORDS] = {0};

    reed_muller_decode(rs_words, codeword);
    reed_solomon_decode(message_words, rs_words);
    memcpy(message, message_words, PARAM_K);
}

static void prefetch_codeword_l2(const uint8_t *codeword, int codeword_bytes)
{
    int lines = (codeword_bytes + 127) / 128;
    if (lines <= 0) {
        return;
    }
    if (lines > 64) {
        lines = 64;
    }

    Q6_l2fetch_AR((void *)codeword, (128 << 16) | (128 << 8) | lines);
}

#if HQC_USE_WORKER_POOL
static void hqc_decode_worker_range(void *data)
{
    hqc_worker_decode_job_t *job = (hqc_worker_decode_job_t *)data;
    int ok = 1;
    int decodes = 0;
    uint32_t sum = 0;

    for (int idx = job->start; idx < job->end; ++idx) {
        const uint8_t *src = job->codewords + (size_t)idx * (size_t)job->codeword_stride;
        uint8_t *dst = job->messages + (size_t)idx * (size_t)job->message_stride;
        size_t fixture = (size_t)idx % HQC_FIXTURE_COUNT;

        hqc_decode_direct(dst, (const uint64_t *)src);
        ok &= memcmp(HQC_FIXTURE_EXPECTED_MESSAGES[fixture], dst, PARAM_K) == 0;
        sum ^= dst[0] ^ ((uint32_t)dst[PARAM_K - 1] << 8) ^ (uint32_t)idx;
        ++decodes;
    }

    job->ok = ok;
    job->decodes = decodes;
    job->checksum = sum;

    if (job->token != 0) {
        worker_pool_synctoken_jobdone(job->token);
    }
}

static int hqc_decode_buffer_bench_worker(const unsigned char *codewords,
                                          unsigned char *messages,
                                          int iters,
                                          int codeword_count,
                                          int codeword_stride,
                                          int message_stride,
                                          int *total_decodes,
                                          int *checksum,
                                          int *passed,
                                          int *mode_status)
{
    worker_pool_context_t context = 0;
    unsigned int ranges = num_hvx128_contexts;
    hqc_worker_decode_job_t jobs[MAX_NUM_WORKERS];
    int ok = 1;
    int decodes = 0;
    uint32_t sum = 0;

    (void)mode_status;

    if (ranges == 0 || ranges > num_workers) {
        ranges = num_workers;
    }
    if (ranges == 0) {
        ranges = 1;
    }
    if (ranges > MAX_NUM_WORKERS) {
        ranges = MAX_NUM_WORKERS;
    }
    if (ranges > (unsigned int)codeword_count) {
        ranges = (unsigned int)codeword_count;
    }

    for (int iter = 0; iter < iters; ++iter) {
        worker_synctoken_t token;
        unsigned int submitted = (ranges > 1) ? (ranges - 1u) : 0u;

        memset(jobs, 0, sizeof(jobs));
        if (submitted != 0) {
            worker_pool_synctoken_init(&token, submitted);
        }

        for (unsigned int r = 0; r < ranges; ++r) {
            int start = (int)(((uint64_t)codeword_count * r) / ranges);
            int end = (int)(((uint64_t)codeword_count * (r + 1u)) / ranges);
            jobs[r].codewords = codewords;
            jobs[r].messages = messages;
            jobs[r].codeword_stride = codeword_stride;
            jobs[r].message_stride = message_stride;
            jobs[r].start = start;
            jobs[r].end = end;
            jobs[r].ok = 1;
            jobs[r].token = (r == 0 || submitted == 0) ? 0 : &token;
        }

        for (unsigned int r = 1; r < ranges; ++r) {
            worker_pool_job_t worker_job;
            worker_job.fptr = hqc_decode_worker_range;
            worker_job.dptr = &jobs[r];
            if (worker_pool_submit(context, worker_job) != 0) {
                jobs[r].token = 0;
                hqc_decode_worker_range(&jobs[r]);
                worker_pool_synctoken_jobdone(&token);
            }
        }

        hqc_decode_worker_range(&jobs[0]);

        if (submitted != 0) {
            worker_pool_synctoken_wait(&token);
        }

        for (unsigned int r = 0; r < ranges; ++r) {
            ok &= jobs[r].ok;
            decodes += jobs[r].decodes;
            sum ^= jobs[r].checksum;
        }
    }

    *total_decodes = decodes;
    *checksum = (int)sum;
    *passed = ok;
    return 0;
}
#endif

static int hqc_decode_buffer_bench_serial(const unsigned char *codewords,
                                          unsigned char *messages,
                                          int iters,
                                          int codeword_count,
                                          int codeword_stride,
                                          int message_stride,
                                          int dsp_mode,
                                          int *total_decodes,
                                          int *checksum,
                                          int *passed)
{
    uint64_t local_codeword[VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {0};
    uint8_t recovered[PARAM_K] __attribute__((aligned(128))) = {0};
    int ok = 1;
    int decodes = 0;
    uint32_t sum = 0;

    for (int iter = 0; iter < iters; ++iter) {
        for (int idx = 0; idx < codeword_count; ++idx) {
            const uint8_t *src = codewords + (size_t)idx * (size_t)codeword_stride;
            uint8_t *dst = messages + (size_t)idx * (size_t)message_stride;
            size_t fixture = (size_t)idx % HQC_FIXTURE_COUNT;

            if (dsp_mode == HQC_BUFFER_MODE_L2FETCH && idx + 1 < codeword_count) {
                const uint8_t *next = codewords + (size_t)(idx + 1) * (size_t)codeword_stride;
                prefetch_codeword_l2(next, VEC_N1N2_SIZE_BYTES);
            }

            if (dsp_mode == HQC_BUFFER_MODE_COPY) {
                memcpy(local_codeword, src, VEC_N1N2_SIZE_BYTES);
                hqc_decode_direct(recovered, local_codeword);
            } else {
                hqc_decode_direct(recovered, (const uint64_t *)src);
            }

            memcpy(dst, recovered, PARAM_K);
            ok &= memcmp(HQC_FIXTURE_EXPECTED_MESSAGES[fixture], recovered, PARAM_K) == 0;
            sum ^= recovered[0] ^ ((uint32_t)recovered[PARAM_K - 1] << 8) ^ (uint32_t)idx;
            ++decodes;
        }
    }

    *total_decodes = decodes;
    *checksum = (int)sum;
    *passed = ok;

    return 0;
}

int hqc_ping(remote_handle64 handle, int iters, int *checksum)
{
    (void)handle;
    uint32_t sum = 0x13579bdfu;

    if (iters <= 0) {
        iters = 1;
    }

    for (int i = 0; i < iters; ++i) {
        sum = (sum << 5) ^ (sum >> 2) ^ (uint32_t)i ^ 0x9e3779b9u;
    }

    *checksum = (int)sum;
    return 0;
}

int hqc_decode_one(remote_handle64 handle,
                   int *rs_symbol_errors,
                   int *checksum,
                   int *passed)
{
    (void)handle;
    uint8_t recovered[PARAM_K] = {0};
    uint64_t codeword_words[VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {0};
    size_t fixture = decode_one_fixture_index;
    int ok;
    uint32_t sum;

    memcpy(codeword_words, HQC_FIXTURE_CODEWORDS[fixture], VEC_N1N2_SIZE_BYTES);
    hqc_decode_direct(recovered, codeword_words);
    ok = memcmp(HQC_FIXTURE_EXPECTED_MESSAGES[fixture], recovered, sizeof(recovered)) == 0;
    sum = recovered[0] ^ ((uint32_t)recovered[PARAM_K - 1] << 8) ^ (uint32_t)fixture;

    decode_one_fixture_index = (fixture + 1u) % HQC_FIXTURE_COUNT;

    *rs_symbol_errors = HQC_FIXTURE_RS_SYMBOL_ERRORS[fixture];
    *checksum = (int)sum;
    *passed = ok;
    return 0;
}

int hqc_decode_bench(remote_handle64 handle,
                     int iters,
                     int *total_decodes,
                     int *total_rs_symbol_errors,
                     int *checksum,
                     int *passed)
{
    (void)handle;
    uint8_t recovered[PARAM_K] = {0};
    uint64_t codeword_words[VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {0};
    int ok = 1;
    int decodes = 0;
    int rs_errors = 0;
    uint32_t sum = 0;

    if (iters <= 0) {
        iters = 1;
    }

    for (int i = 0; i < iters; ++i) {
        for (size_t fixture = 0; fixture < HQC_FIXTURE_COUNT; ++fixture) {
            memcpy(codeword_words, HQC_FIXTURE_CODEWORDS[fixture], VEC_N1N2_SIZE_BYTES);
            hqc_decode_direct(recovered, codeword_words);
            ok &= memcmp(HQC_FIXTURE_EXPECTED_MESSAGES[fixture], recovered, sizeof(recovered)) == 0;
            rs_errors += HQC_FIXTURE_RS_SYMBOL_ERRORS[fixture];
            sum ^= recovered[0] ^ ((uint32_t)recovered[PARAM_K - 1] << 8);
            ++decodes;
        }
    }

    *total_decodes = decodes;
    *total_rs_symbol_errors = rs_errors;
    *checksum = (int)sum;
    *passed = ok;

    return 0;
}

int hqc_payload_in(remote_handle64 handle,
                   const unsigned char *input,
                   int inputLen,
                   int iters,
                   int *checksum)
{
    (void)handle;
    uint32_t sum = 0x31415926u ^ (uint32_t)inputLen;

    if (input == 0 || inputLen <= 0) {
        *checksum = 0;
        return 0;
    }
    if (iters <= 0) {
        iters = 1;
    }

    for (int iter = 0; iter < iters; ++iter) {
        for (int i = 0; i < inputLen; ++i) {
            sum = (sum << 5) ^ (sum >> 2) ^ input[i] ^ (uint32_t)i;
        }
    }

    *checksum = (int)sum;
    return 0;
}

int hqc_payload_out(remote_handle64 handle,
                    unsigned char *output,
                    int outputLen,
                    int iters,
                    int *checksum)
{
    (void)handle;
    uint32_t sum = 0x27182818u ^ (uint32_t)outputLen;

    if (output == 0 || outputLen <= 0) {
        *checksum = 0;
        return 0;
    }
    if (iters <= 0) {
        iters = 1;
    }

    for (int iter = 0; iter < iters; ++iter) {
        for (int i = 0; i < outputLen; ++i) {
            uint8_t value = (uint8_t)(sum + (uint32_t)i + (uint32_t)iter);
            output[i] = value;
            sum = (sum << 3) ^ (sum >> 7) ^ value;
        }
    }

    *checksum = (int)sum;
    return 0;
}

int hqc_payload_inout(remote_handle64 handle,
                      const unsigned char *input,
                      int inputLen,
                      unsigned char *output,
                      int outputLen,
                      int iters,
                      int *checksum)
{
    (void)handle;
    uint32_t sum = 0x9e3779b9u ^ (uint32_t)inputLen ^ ((uint32_t)outputLen << 1);

    if (input == 0 || output == 0 || inputLen <= 0 || outputLen <= 0) {
        *checksum = 0;
        return 0;
    }
    if (iters <= 0) {
        iters = 1;
    }

    for (int iter = 0; iter < iters; ++iter) {
        for (int i = 0; i < inputLen; ++i) {
            sum = (sum << 5) ^ (sum >> 2) ^ input[i] ^ (uint32_t)i;
        }
        for (int i = 0; i < outputLen; ++i) {
            uint8_t value = (uint8_t)(sum + (uint32_t)i);
            output[i] = value;
            sum = (sum << 3) ^ (sum >> 7) ^ value;
        }
    }

    *checksum = (int)sum;
    return 0;
}

int hqc_decode_buffer_bench(remote_handle64 handle,
                            const unsigned char *codewords,
                            int codewordsLen,
                            unsigned char *messages,
                            int messagesLen,
                            int iters,
                            int codeword_count,
                            int codeword_stride,
                            int message_stride,
                            int dsp_mode,
                            int *total_decodes,
                            int *checksum,
                            int *passed,
                            int *mode_status)
{
    (void)handle;

    *total_decodes = 0;
    *checksum = 0;
    *passed = 0;
    *mode_status = HQC_BUFFER_STATUS_OK;

    if (dsp_mode == HQC_BUFFER_MODE_VTCM) {
        *mode_status = HQC_BUFFER_STATUS_UNSUPPORTED;
        return 0;
    }

    if (codewords == 0 || messages == 0 ||
        iters <= 0 || codeword_count <= 0 ||
        codeword_stride < VEC_N1N2_SIZE_BYTES ||
        message_stride < PARAM_K ||
        codewordsLen < codeword_count * codeword_stride ||
        messagesLen < codeword_count * message_stride ||
        (dsp_mode != HQC_BUFFER_MODE_DIRECT &&
         dsp_mode != HQC_BUFFER_MODE_COPY &&
         dsp_mode != HQC_BUFFER_MODE_L2FETCH &&
         dsp_mode != HQC_BUFFER_MODE_WORKER_POOL)) {
        *mode_status = HQC_BUFFER_STATUS_BAD_ARGS;
        return 0;
    }

    if (dsp_mode == HQC_BUFFER_MODE_WORKER_POOL) {
#if HQC_USE_WORKER_POOL
        return hqc_decode_buffer_bench_worker(codewords,
                                              messages,
                                              iters,
                                              codeword_count,
                                              codeword_stride,
                                              message_stride,
                                              total_decodes,
                                              checksum,
                                              passed,
                                              mode_status);
#else
        *mode_status = HQC_BUFFER_STATUS_UNSUPPORTED;
        return 0;
#endif
    }

    return hqc_decode_buffer_bench_serial(codewords,
                                          messages,
                                          iters,
                                          codeword_count,
                                          codeword_stride,
                                          message_stride,
                                          dsp_mode,
                                          total_decodes,
                                          checksum,
                                          passed);
}
