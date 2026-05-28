#include <stdint.h>
#include <string.h>

#include <hexagon_protos.h>

#include "hqc.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#ifndef HQC_PARAM_LEVEL
#define HQC_PARAM_LEVEL 128
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

#define HQC_BUFFER_STATUS_OK 0
#define HQC_BUFFER_STATUS_BAD_ARGS -1
#define HQC_BUFFER_STATUS_UNSUPPORTED -2

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
    uint64_t local_codeword[VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {0};
    uint8_t recovered[PARAM_K] __attribute__((aligned(128))) = {0};
    int ok = 1;
    int decodes = 0;
    uint32_t sum = 0;

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
         dsp_mode != HQC_BUFFER_MODE_L2FETCH)) {
        *mode_status = HQC_BUFFER_STATUS_BAD_ARGS;
        return 0;
    }

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
