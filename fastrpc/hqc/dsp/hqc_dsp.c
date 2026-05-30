#include <stdint.h>
#include <string.h>

#include <hexagon_protos.h>

#include "data_structures.h"
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
#define MULTIPLICITY CEIL_DIVIDE(PARAM_N2, 128)

#define HQC_BUFFER_MODE_DIRECT 0
#define HQC_BUFFER_MODE_COPY 1
#define HQC_BUFFER_MODE_L2FETCH 2
#define HQC_BUFFER_MODE_VTCM 3
#define HQC_BUFFER_STATUS_OK 0
#define HQC_BUFFER_STATUS_BAD_ARGS -1
#define HQC_BUFFER_STATUS_UNSUPPORTED -2

static size_t decode_one_fixture_index = 0;

typedef int16_t rm_expanded_cdw[128];

static uint64_t substage_codeword_words[HQC_FIXTURE_COUNT][VEC_N1N2_SIZE_64] __attribute__((aligned(128)));
static uint64_t substage_rs_words[HQC_FIXTURE_COUNT][VEC_N1_SIZE_64] __attribute__((aligned(128)));
static rm_expanded_cdw substage_rm_expanded_ref[HQC_FIXTURE_COUNT][PARAM_N1] __attribute__((aligned(128)));
static rm_expanded_cdw substage_rm_transform_ref[HQC_FIXTURE_COUNT][PARAM_N1] __attribute__((aligned(128)));
static uint8_t substage_rs_cdw_ref[HQC_FIXTURE_COUNT][PARAM_N1];
static uint16_t substage_rs_syndromes_ref[HQC_FIXTURE_COUNT][2 * PARAM_DELTA];
static uint16_t substage_rs_sigma_ref[HQC_FIXTURE_COUNT][1 << PARAM_SIGMA_SIZE_LOG];
static uint8_t substage_rs_error_ref[HQC_FIXTURE_COUNT][1 << PARAM_M];
static uint16_t substage_rs_z_ref[HQC_FIXTURE_COUNT][PARAM_N1];
static uint16_t substage_rs_error_values_ref[HQC_FIXTURE_COUNT][PARAM_N1];
static uint8_t substage_rs_corrected_ref[HQC_FIXTURE_COUNT][PARAM_N1];
static uint16_t substage_rs_degree_ref[HQC_FIXTURE_COUNT];

void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]);
void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst);
int32_t find_peaks_hvx(rm_expanded_cdw *transform);

static void rm_expand(rm_expanded_cdw *dest, rm_codeword_t *src)
{
    expand_and_sum_hvx(dest, src);
}

static void rm_hadamard(rm_expanded_cdw *src, rm_expanded_cdw *dst)
{
    hadamard_hvx(src, dst);
}

static int32_t rm_peak(rm_expanded_cdw *transform)
{
    return find_peaks_hvx(transform);
}

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

int hqc_substage_bench(remote_handle64 handle,
                       int stage,
                       int iters,
                       int *total_ops,
                       int *checksum,
                       int *passed)
{
    (void)handle;
    uint64_t message_words[MSG_WORDS] = {0};
    int ok = 1;
    uint32_t sum = 0;
    int ops = 0;

    if (iters <= 0) {
        iters = 1;
    }

    if (stage < 1 || stage > 9) {
        *total_ops = 0;
        *checksum = 0;
        *passed = 0;
        return 0;
    }

    for (size_t fixture = 0; fixture < HQC_FIXTURE_COUNT; ++fixture) {
        memcpy(substage_codeword_words[fixture], HQC_FIXTURE_CODEWORDS[fixture], VEC_N1N2_SIZE_BYTES);
        reed_muller_decode(substage_rs_words[fixture], substage_codeword_words[fixture]);
        reed_solomon_decode(message_words, substage_rs_words[fixture]);
        ok &= memcmp(message_words, HQC_FIXTURE_EXPECTED_MESSAGES[fixture], PARAM_K) == 0;

        rm_codeword_t *code_array = (rm_codeword_t *)substage_codeword_words[fixture];
        for (size_t block = 0; block < PARAM_N1; ++block) {
            rm_expanded_cdw setup;
            rm_expand(&substage_rm_expanded_ref[fixture][block], &code_array[block * MULTIPLICITY]);
            memcpy(setup, substage_rm_expanded_ref[fixture][block], sizeof(setup));
            rm_hadamard(&setup, &substage_rm_transform_ref[fixture][block]);
            substage_rm_transform_ref[fixture][block][0] -= 64 * MULTIPLICITY;
            sum ^= (uint8_t)rm_peak(&substage_rm_transform_ref[fixture][block]);
        }

        memcpy(substage_rs_cdw_ref[fixture], substage_rs_words[fixture], PARAM_N1);
        hqc_rs_bench_compute_syndromes(substage_rs_syndromes_ref[fixture], substage_rs_cdw_ref[fixture]);
        substage_rs_degree_ref[fixture] = hqc_rs_bench_compute_elp(substage_rs_sigma_ref[fixture], substage_rs_syndromes_ref[fixture]);
        hqc_rs_bench_compute_roots(substage_rs_error_ref[fixture], substage_rs_sigma_ref[fixture], substage_rs_degree_ref[fixture]);
        hqc_rs_bench_compute_z_poly(substage_rs_z_ref[fixture], substage_rs_sigma_ref[fixture], substage_rs_degree_ref[fixture], substage_rs_syndromes_ref[fixture]);
        hqc_rs_bench_compute_error_values(substage_rs_error_values_ref[fixture], substage_rs_z_ref[fixture], substage_rs_error_ref[fixture], substage_rs_sigma_ref[fixture], substage_rs_degree_ref[fixture]);
        memcpy(substage_rs_corrected_ref[fixture], substage_rs_cdw_ref[fixture], PARAM_N1);
        hqc_rs_bench_correct_errors(substage_rs_corrected_ref[fixture], substage_rs_error_values_ref[fixture]);
        ok &= memcmp(substage_rs_corrected_ref[fixture] + (PARAM_G - 1), HQC_FIXTURE_EXPECTED_MESSAGES[fixture], PARAM_K) == 0;
    }

    for (int iter = 0; iter < iters; ++iter) {
        for (size_t fixture = 0; fixture < HQC_FIXTURE_COUNT; ++fixture) {
            switch (stage) {
                case 1: {
                    rm_codeword_t *code_array = (rm_codeword_t *)substage_codeword_words[fixture];
                    rm_expanded_cdw out;
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        rm_expand(&out, &code_array[block * MULTIPLICITY]);
                        sum ^= (uint16_t)out[0] ^ ((uint32_t)(uint16_t)out[127] << 16);
                        ++ops;
                    }
                    break;
                }
                case 2: {
                    rm_expanded_cdw src;
                    rm_expanded_cdw out;
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        memcpy(src, substage_rm_expanded_ref[fixture][block], sizeof(src));
                        rm_hadamard(&src, &out);
                        out[0] -= 64 * MULTIPLICITY;
                        sum ^= (uint16_t)out[0] ^ ((uint32_t)(uint16_t)out[127] << 16);
                        ++ops;
                    }
                    break;
                }
                case 3: {
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        sum ^= (uint8_t)rm_peak(&substage_rm_transform_ref[fixture][block]);
                        ++ops;
                    }
                    break;
                }
                case 4: {
                    uint16_t syndromes[2 * PARAM_DELTA] = {0};
                    hqc_rs_bench_compute_syndromes(syndromes, substage_rs_cdw_ref[fixture]);
                    sum ^= syndromes[0] ^ ((uint32_t)syndromes[2 * PARAM_DELTA - 1] << 16);
                    ++ops;
                    break;
                }
                case 5: {
                    uint16_t sigma[1 << PARAM_SIGMA_SIZE_LOG] = {0};
                    uint16_t degree = hqc_rs_bench_compute_elp(sigma, substage_rs_syndromes_ref[fixture]);
                    sum ^= degree ^ sigma[0] ^ ((uint32_t)sigma[PARAM_DELTA] << 16);
                    ++ops;
                    break;
                }
                case 6: {
                    uint8_t error[1 << PARAM_M] = {0};
                    uint16_t sigma[1 << PARAM_SIGMA_SIZE_LOG] = {0};
                    memcpy(sigma, substage_rs_sigma_ref[fixture], sizeof(sigma));
                    hqc_rs_bench_compute_roots(error, sigma, substage_rs_degree_ref[fixture]);
                    sum ^= error[0] ^ ((uint32_t)error[PARAM_N1 - 1] << 16);
                    ++ops;
                    break;
                }
                case 7: {
                    uint16_t z[PARAM_N1] = {0};
                    hqc_rs_bench_compute_z_poly(z, substage_rs_sigma_ref[fixture], substage_rs_degree_ref[fixture], substage_rs_syndromes_ref[fixture]);
                    sum ^= z[0] ^ ((uint32_t)z[PARAM_DELTA] << 16);
                    ++ops;
                    break;
                }
                case 8: {
                    uint16_t error_values[PARAM_N1] = {0};
                    hqc_rs_bench_compute_error_values(error_values, substage_rs_z_ref[fixture], substage_rs_error_ref[fixture], substage_rs_sigma_ref[fixture], substage_rs_degree_ref[fixture]);
                    sum ^= error_values[0] ^ ((uint32_t)error_values[PARAM_N1 - 1] << 16);
                    ++ops;
                    break;
                }
                case 9: {
                    uint8_t corrected[PARAM_N1] = {0};
                    memcpy(corrected, substage_rs_cdw_ref[fixture], PARAM_N1);
                    hqc_rs_bench_correct_errors(corrected, substage_rs_error_values_ref[fixture]);
                    sum ^= corrected[0] ^ ((uint32_t)corrected[PARAM_N1 - 1] << 16);
                    ++ops;
                    break;
                }
            }
        }
    }

    *total_ops = ops;
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
         dsp_mode != HQC_BUFFER_MODE_L2FETCH)) {
        *mode_status = HQC_BUFFER_STATUS_BAD_ARGS;
        return 0;
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
