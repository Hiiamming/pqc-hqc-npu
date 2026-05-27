#include <stdio.h>
#include <string.h>

#include "hqc3_decode_fixture.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#ifndef HQC3_BENCH_ITERS
#define HQC3_BENCH_ITERS 100
#endif

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

static void hqc3_decode_direct(uint8_t message[PARAM_K], const uint64_t codeword[VEC_N1N2_SIZE_64])
{
    uint64_t rs_words[VEC_N1_SIZE_64] = {0};
    uint64_t message_words[MSG_WORDS] = {0};

    reed_muller_decode(rs_words, codeword);
    reed_solomon_decode(message_words, rs_words);
    memcpy(message, message_words, PARAM_K);
}

int main(void)
{
    uint8_t recovered[PARAM_K] = {0};
    uint64_t codeword_words[VEC_N1N2_SIZE_64] __attribute__((aligned(128))) = {0};
    int ok = 1;
    unsigned total_decodes = 0;
    unsigned total_rs_errors = 0;

    for (int i = 0; i < HQC3_BENCH_ITERS; ++i) {
        for (size_t fixture = 0; fixture < HQC3_FIXTURE_COUNT; ++fixture) {
            memcpy(codeword_words, hqc3_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
            hqc3_decode_direct(recovered, codeword_words);
            ok &= memcmp(hqc3_fixture_expected_messages[fixture], recovered, sizeof(recovered)) == 0;
            total_rs_errors += hqc3_fixture_rs_symbol_errors[fixture];
            ++total_decodes;
        }
    }

    printf("[decode-bench] iters=%d fixtures=%d total_decodes=%u total_rs_symbol_errors=%u result=%s\n",
           HQC3_BENCH_ITERS,
           HQC3_FIXTURE_COUNT,
           total_decodes,
           total_rs_errors,
           ok ? "PASS" : "FAIL");

    if (!ok) {
        printf("[decode-bench] at least one fixture failed\n");
    }

    return ok ? 0 : 1;
}
