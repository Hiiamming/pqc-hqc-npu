#include <stdio.h>
#include <string.h>

#include "hqc256_decode_fixture.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#ifndef HQC256_BENCH_ITERS
#define HQC256_BENCH_ITERS 100
#endif

#ifndef HQC256_STAGE
#define HQC256_STAGE 0
#endif

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

static void decode_full(uint8_t message[PARAM_K], const uint64_t codeword[VEC_N1N2_SIZE_64])
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
    uint64_t rs_references[HQC256_FIXTURE_COUNT][VEC_N1_SIZE_64] __attribute__((aligned(128))) = {{0}};
    uint64_t rs_words[VEC_N1_SIZE_64] = {0};
    uint64_t message_words[MSG_WORDS] = {0};
    int ok = 1;
    unsigned total_decodes = 0;

    for (size_t fixture = 0; fixture < HQC256_FIXTURE_COUNT; ++fixture) {
        memcpy(codeword_words, hqc256_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
        reed_muller_decode(rs_references[fixture], codeword_words);
        reed_solomon_decode(message_words, rs_references[fixture]);
        memcpy(recovered, message_words, PARAM_K);

        if (memcmp(hqc256_fixture_expected_messages[fixture], recovered, sizeof(recovered)) != 0) {
            printf("[stage-bench] setup fixture=%zu result=FAIL\n", fixture);
            return 1;
        }
    }

    for (int i = 0; i < HQC256_BENCH_ITERS; ++i) {
        for (size_t fixture = 0; fixture < HQC256_FIXTURE_COUNT; ++fixture) {
#if HQC256_STAGE == 1
            memcpy(codeword_words, hqc256_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
            reed_muller_decode(rs_words, codeword_words);
            ok &= memcmp(rs_words, rs_references[fixture], VEC_N1_SIZE_BYTES) == 0;
#elif HQC256_STAGE == 2
            reed_solomon_decode(message_words, rs_references[fixture]);
            memcpy(recovered, message_words, PARAM_K);
            ok &= memcmp(hqc256_fixture_expected_messages[fixture], recovered, sizeof(recovered)) == 0;
#else
            memcpy(codeword_words, hqc256_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
            decode_full(recovered, codeword_words);
            ok &= memcmp(hqc256_fixture_expected_messages[fixture], recovered, sizeof(recovered)) == 0;
#endif
            ++total_decodes;
        }
    }

#if HQC256_STAGE == 1
    const char *stage = "rm";
#elif HQC256_STAGE == 2
    const char *stage = "rs";
#else
    const char *stage = "full";
#endif

    printf("[stage-bench] stage=%s iters=%d fixtures=%d total_decodes=%u result=%s\n",
           stage,
           HQC256_BENCH_ITERS,
           HQC256_FIXTURE_COUNT,
           total_decodes,
           ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
