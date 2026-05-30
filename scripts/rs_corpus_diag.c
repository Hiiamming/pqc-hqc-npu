#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "parameters.h"
#include "gf.h"
#include "reed_muller.h"
#include "reed_solomon.h"

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

static void compute_syndromes_local(uint16_t *syndromes, uint8_t *cdw)
{
    for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
        syndromes[i] = 0;
        for (size_t j = 1; j < PARAM_N1; ++j) {
            syndromes[i] ^= gf_mul(cdw[j], alpha_ij_pow[i][j - 1]);
        }
        syndromes[i] ^= cdw[0];
    }
}

static uint16_t compute_elp_local(uint16_t *sigma, const uint16_t *syndromes)
{
    uint16_t b[PARAM_DELTA + 1] = {0};
    uint16_t t[PARAM_DELTA + 1] = {0};
    uint16_t deg_sigma = 0;
    uint16_t deg_b = 0;
    uint16_t m = 1;
    uint16_t d_p = 1;

    sigma[0] = 1;
    b[0] = 1;

    for (uint16_t mu = 0; mu < (2 * PARAM_DELTA); ++mu) {
        uint16_t d = syndromes[mu];

        for (uint16_t i = 1; i <= deg_sigma; ++i) {
            d ^= gf_mul(sigma[i], syndromes[mu - i]);
        }

        if (d != 0) {
            uint16_t dd = gf_mul(d, gf_inverse(d_p));

            memcpy(t, sigma, sizeof(t));
            uint16_t update_degree = m + deg_b;
            if (update_degree > PARAM_DELTA) {
                update_degree = PARAM_DELTA;
            }
            for (uint16_t i = m; i <= update_degree; ++i) {
                sigma[i] ^= gf_mul(dd, b[i - m]);
            }

            if ((uint16_t)(2 * deg_sigma) <= mu) {
                uint16_t old_deg_sigma = deg_sigma;
                deg_sigma = mu + 1 - deg_sigma;
                memcpy(b, t, sizeof(b));
                deg_b = old_deg_sigma;
                m = 1;
                d_p = d;
            } else {
                ++m;
            }
        } else {
            ++m;
        }
    }

    return deg_sigma;
}

int main(void)
{
    unsigned declared_nonzero = 0;
    unsigned actual_nonzero_syndrome = 0;
    unsigned declared_but_clean = 0;
    unsigned degree_hist[PARAM_DELTA + 1];
    unsigned syndrome_weight_hist[2 * PARAM_DELTA + 1];
    unsigned max_syndrome_weight = 0;

    memset(degree_hist, 0, sizeof(degree_hist));
    memset(syndrome_weight_hist, 0, sizeof(syndrome_weight_hist));

    for (size_t fixture = 0; fixture < HQC_FIXTURE_COUNT; ++fixture) {
        uint64_t codeword_words[VEC_N1N2_SIZE_64] = {0};
        uint64_t rs_words[VEC_N1_SIZE_64] = {0};
        uint64_t message_words[MSG_WORDS] = {0};
        uint8_t rs_cdw[PARAM_N1] = {0};
        uint16_t syndromes[2 * PARAM_DELTA] = {0};
        uint16_t sigma[1 << PARAM_M] = {0};
        unsigned syndrome_weight = 0;
        uint16_t degree;

        memcpy(codeword_words, HQC_FIXTURE_CODEWORDS[fixture], VEC_N1N2_SIZE_BYTES);
        reed_muller_decode(rs_words, codeword_words);
        reed_solomon_decode(message_words, rs_words);
        if (memcmp(message_words, HQC_FIXTURE_EXPECTED_MESSAGES[fixture], PARAM_K) != 0) {
            printf("decode_mismatch fixture=%zu\n", fixture);
            return 1;
        }

        memcpy(rs_cdw, rs_words, PARAM_N1);
        compute_syndromes_local(syndromes, rs_cdw);
        for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
            syndrome_weight += syndromes[i] != 0;
        }
        degree = compute_elp_local(sigma, syndromes);

        declared_nonzero += HQC_FIXTURE_RS_SYMBOL_ERRORS[fixture] != 0;
        actual_nonzero_syndrome += syndrome_weight != 0;
        declared_but_clean += (HQC_FIXTURE_RS_SYMBOL_ERRORS[fixture] != 0) && (syndrome_weight == 0);
        if (degree <= PARAM_DELTA) {
            ++degree_hist[degree];
        }
        if (syndrome_weight <= 2 * PARAM_DELTA) {
            ++syndrome_weight_hist[syndrome_weight];
        }
        if (syndrome_weight > max_syndrome_weight) {
            max_syndrome_weight = syndrome_weight;
        }
    }

    printf("level=%d fixtures=%u declared_nonzero=%u actual_nonzero_syndrome=%u declared_but_clean=%u max_syndrome_weight=%u\n",
           HQC_PARAM_LEVEL,
           (unsigned)HQC_FIXTURE_COUNT,
           declared_nonzero,
           actual_nonzero_syndrome,
           declared_but_clean,
           max_syndrome_weight);

    printf("degree_hist:");
    for (size_t i = 0; i <= PARAM_DELTA; ++i) {
        if (degree_hist[i] != 0) {
            printf(" %zu:%u", i, degree_hist[i]);
        }
    }
    printf("\n");

    printf("syndrome_weight_hist:");
    for (size_t i = 0; i <= 2 * PARAM_DELTA; ++i) {
        if (syndrome_weight_hist[i] != 0) {
            printf(" %zu:%u", i, syndrome_weight_hist[i]);
        }
    }
    printf("\n");

    return 0;
}
