#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "data_structures.h"
#include "hqc128_decode_fixture.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#ifndef HQC128_BENCH_ITERS
#define HQC128_BENCH_ITERS 10
#endif

#ifndef HQC128_SUBSTAGE
#define HQC128_SUBSTAGE 0
#endif

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)
#define MULTIPLICITY CEIL_DIVIDE(PARAM_N2, 128)

typedef int16_t rm_expanded_cdw[128];

void expand_and_sum(rm_expanded_cdw *dest, rm_codeword_t src[]);
void hadamard(rm_expanded_cdw *src, rm_expanded_cdw *dst);
int32_t find_peaks(rm_expanded_cdw *transform);

static const char *stage_name(void) {
    switch (HQC128_SUBSTAGE) {
        case 1:
            return "rm_expand";
        case 2:
            return "rm_hadamard";
        case 3:
            return "rm_peak";
        case 4:
            return "rs_syndrome";
        case 5:
            return "rs_elp";
        case 6:
            return "rs_roots";
        case 7:
            return "rs_z";
        case 8:
            return "rs_error_values";
        case 9:
            return "rs_correct";
        default:
            return "unknown";
    }
}

int main(void) {
    uint64_t codeword_words[HQC128_FIXTURE_COUNT][VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {{0}};
    uint64_t rs_words[HQC128_FIXTURE_COUNT][VEC_N1_SIZE_64] __attribute__((aligned(128))) = {{0}};
    uint64_t message_words[MSG_WORDS] = {0};

    rm_expanded_cdw rm_expanded_ref[HQC128_FIXTURE_COUNT][PARAM_N1] __attribute__((aligned(128)));
    rm_expanded_cdw rm_transform_ref[HQC128_FIXTURE_COUNT][PARAM_N1] __attribute__((aligned(128)));

    uint8_t rs_cdw_ref[HQC128_FIXTURE_COUNT][PARAM_N1] = {{0}};
    uint16_t rs_syndromes_ref[HQC128_FIXTURE_COUNT][2 * PARAM_DELTA] = {{0}};
    uint16_t rs_sigma_ref[HQC128_FIXTURE_COUNT][1 << PARAM_FFT] = {{0}};
    uint8_t rs_error_ref[HQC128_FIXTURE_COUNT][1 << PARAM_M] = {{0}};
    uint16_t rs_z_ref[HQC128_FIXTURE_COUNT][PARAM_N1] = {{0}};
    uint16_t rs_error_values_ref[HQC128_FIXTURE_COUNT][PARAM_N1] = {{0}};
    uint8_t rs_corrected_ref[HQC128_FIXTURE_COUNT][PARAM_N1] = {{0}};
    uint16_t rs_degree_ref[HQC128_FIXTURE_COUNT] = {0};

    int ok = 1;
    unsigned total_ops = 0;
    volatile uint32_t checksum = 0;

    for (size_t fixture = 0; fixture < HQC128_FIXTURE_COUNT; ++fixture) {
        memcpy(codeword_words[fixture], hqc128_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
        reed_muller_decode(rs_words[fixture], codeword_words[fixture]);
        reed_solomon_decode(message_words, rs_words[fixture]);
        ok &= memcmp(message_words, hqc128_fixture_expected_messages[fixture], PARAM_K) == 0;

        rm_codeword_t *code_array = (rm_codeword_t *)codeword_words[fixture];
        for (size_t block = 0; block < PARAM_N1; ++block) {
            rm_expanded_cdw setup;
            expand_and_sum(&rm_expanded_ref[fixture][block], &code_array[block * MULTIPLICITY]);
            memcpy(setup, rm_expanded_ref[fixture][block], sizeof(setup));
            hadamard(&setup, &rm_transform_ref[fixture][block]);
            rm_transform_ref[fixture][block][0] -= 64 * MULTIPLICITY;
            checksum ^= (uint8_t)find_peaks(&rm_transform_ref[fixture][block]);
        }

        memcpy(rs_cdw_ref[fixture], rs_words[fixture], PARAM_N1);
        hqc_rs_bench_compute_syndromes(rs_syndromes_ref[fixture], rs_cdw_ref[fixture]);
        rs_degree_ref[fixture] = hqc_rs_bench_compute_elp(rs_sigma_ref[fixture], rs_syndromes_ref[fixture]);
        hqc_rs_bench_compute_roots(rs_error_ref[fixture], rs_sigma_ref[fixture], rs_degree_ref[fixture]);
        hqc_rs_bench_compute_z_poly(rs_z_ref[fixture], rs_sigma_ref[fixture], rs_degree_ref[fixture], rs_syndromes_ref[fixture]);
        hqc_rs_bench_compute_error_values(rs_error_values_ref[fixture], rs_z_ref[fixture], rs_error_ref[fixture], rs_sigma_ref[fixture], rs_degree_ref[fixture]);
        memcpy(rs_corrected_ref[fixture], rs_cdw_ref[fixture], PARAM_N1);
        hqc_rs_bench_correct_errors(rs_corrected_ref[fixture], rs_error_values_ref[fixture]);
        ok &= memcmp(rs_corrected_ref[fixture] + (PARAM_G - 1), hqc128_fixture_expected_messages[fixture], PARAM_K) == 0;
    }

    for (int iter = 0; iter < HQC128_BENCH_ITERS; ++iter) {
        for (size_t fixture = 0; fixture < HQC128_FIXTURE_COUNT; ++fixture) {
            switch (HQC128_SUBSTAGE) {
                case 1: {
                    rm_codeword_t *code_array = (rm_codeword_t *)codeword_words[fixture];
                    rm_expanded_cdw out;
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        expand_and_sum(&out, &code_array[block * MULTIPLICITY]);
                        checksum ^= (uint16_t)out[0] ^ ((uint32_t)(uint16_t)out[127] << 16);
                        ++total_ops;
                    }
                    break;
                }
                case 2: {
                    rm_expanded_cdw src;
                    rm_expanded_cdw out;
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        memcpy(src, rm_expanded_ref[fixture][block], sizeof(src));
                        hadamard(&src, &out);
                        out[0] -= 64 * MULTIPLICITY;
                        checksum ^= (uint16_t)out[0] ^ ((uint32_t)(uint16_t)out[127] << 16);
                        ++total_ops;
                    }
                    break;
                }
                case 3: {
                    for (size_t block = 0; block < PARAM_N1; ++block) {
                        checksum ^= (uint8_t)find_peaks(&rm_transform_ref[fixture][block]);
                        ++total_ops;
                    }
                    break;
                }
                case 4: {
                    uint16_t syndromes[2 * PARAM_DELTA] = {0};
                    hqc_rs_bench_compute_syndromes(syndromes, rs_cdw_ref[fixture]);
                    checksum ^= syndromes[0] ^ ((uint32_t)syndromes[2 * PARAM_DELTA - 1] << 16);
                    ++total_ops;
                    break;
                }
                case 5: {
                    uint16_t sigma[1 << PARAM_FFT] = {0};
                    uint16_t degree = hqc_rs_bench_compute_elp(sigma, rs_syndromes_ref[fixture]);
                    checksum ^= degree ^ sigma[0] ^ ((uint32_t)sigma[PARAM_DELTA] << 16);
                    ++total_ops;
                    break;
                }
                case 6: {
                    uint8_t error[1 << PARAM_M] = {0};
                    uint16_t sigma[1 << PARAM_FFT] = {0};
                    memcpy(sigma, rs_sigma_ref[fixture], sizeof(sigma));
                    hqc_rs_bench_compute_roots(error, sigma, rs_degree_ref[fixture]);
                    checksum ^= error[0] ^ ((uint32_t)error[PARAM_N1 - 1] << 16);
                    ++total_ops;
                    break;
                }
                case 7: {
                    uint16_t z[PARAM_N1] = {0};
                    hqc_rs_bench_compute_z_poly(z, rs_sigma_ref[fixture], rs_degree_ref[fixture], rs_syndromes_ref[fixture]);
                    checksum ^= z[0] ^ ((uint32_t)z[PARAM_DELTA] << 16);
                    ++total_ops;
                    break;
                }
                case 8: {
                    uint16_t error_values[PARAM_N1] = {0};
                    hqc_rs_bench_compute_error_values(error_values, rs_z_ref[fixture], rs_error_ref[fixture], rs_sigma_ref[fixture], rs_degree_ref[fixture]);
                    checksum ^= error_values[0] ^ ((uint32_t)error_values[PARAM_N1 - 1] << 16);
                    ++total_ops;
                    break;
                }
                case 9: {
                    uint8_t corrected[PARAM_N1] = {0};
                    memcpy(corrected, rs_cdw_ref[fixture], PARAM_N1);
                    hqc_rs_bench_correct_errors(corrected, rs_error_values_ref[fixture]);
                    checksum ^= corrected[0] ^ ((uint32_t)corrected[PARAM_N1 - 1] << 16);
                    ++total_ops;
                    break;
                }
                default:
                    printf("[substage-bench] unknown stage=%d\n", HQC128_SUBSTAGE);
                    return 1;
            }
        }
    }

    printf("[substage-bench] stage=%s iters=%d fixtures=%d total_ops=%u checksum=0x%08x result=%s\n",
           stage_name(),
           HQC128_BENCH_ITERS,
           HQC128_FIXTURE_COUNT,
           total_ops,
           (unsigned)checksum,
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
