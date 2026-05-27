#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "hqc1_decode_fixture.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#ifndef HQC1_BENCH_ITERS
#define HQC1_BENCH_ITERS 100
#endif

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

static uint64_t monotonic_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static void hqc1_decode_direct(uint8_t message[PARAM_K], const uint64_t codeword[VEC_N1N2_SIZE_64])
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
    volatile uint32_t checksum = 0;

    uint64_t start_ns = monotonic_ns();
    for (int i = 0; i < HQC1_BENCH_ITERS; ++i) {
        for (size_t fixture = 0; fixture < HQC1_FIXTURE_COUNT; ++fixture) {
            memcpy(codeword_words, hqc1_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
            hqc1_decode_direct(recovered, codeword_words);
            ok &= memcmp(hqc1_fixture_expected_messages[fixture], recovered, sizeof(recovered)) == 0;
            total_rs_errors += hqc1_fixture_rs_symbol_errors[fixture];
            checksum ^= recovered[0] ^ ((uint32_t)recovered[PARAM_K - 1] << 8);
            ++total_decodes;
        }
    }
    uint64_t elapsed_ns = monotonic_ns() - start_ns;

    double elapsed_ms = (double)elapsed_ns / 1000000.0;
    double ns_per_decode = total_decodes ? (double)elapsed_ns / (double)total_decodes : 0.0;

    printf("[arm64-scalar-decode] iters=%d fixtures=%d total_decodes=%u total_rs_symbol_errors=%u checksum=0x%08x result=%s\n",
           HQC1_BENCH_ITERS,
           HQC1_FIXTURE_COUNT,
           total_decodes,
           total_rs_errors,
           (unsigned)checksum,
           ok ? "PASS" : "FAIL");
    printf("[arm64-scalar-decode] elapsed_ms=%.3f ns_per_decode=%.1f us_per_decode=%.3f\n",
           elapsed_ms,
           ns_per_decode,
           ns_per_decode / 1000.0);

    return ok ? 0 : 1;
}
