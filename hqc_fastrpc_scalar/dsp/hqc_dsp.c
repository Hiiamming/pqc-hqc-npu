#include <stdint.h>
#include <string.h>

#include "hqc.h"
#include "hqc128_decode_fixture.h"
#include "parameters.h"
#include "reed_muller.h"
#include "reed_solomon.h"

#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

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

static void hqc128_decode_direct(uint8_t message[PARAM_K], const uint64_t codeword[VEC_N1N2_SIZE_64])
{
    uint64_t rs_words[VEC_N1_SIZE_64] = {0};
    uint64_t message_words[MSG_WORDS] = {0};

    reed_muller_decode(rs_words, codeword);
    reed_solomon_decode(message_words, rs_words);
    memcpy(message, message_words, PARAM_K);
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
        for (size_t fixture = 0; fixture < HQC128_FIXTURE_COUNT; ++fixture) {
            memcpy(codeword_words, hqc128_fixture_codewords[fixture], VEC_N1N2_SIZE_BYTES);
            hqc128_decode_direct(recovered, codeword_words);
            ok &= memcmp(hqc128_fixture_expected_messages[fixture], recovered, sizeof(recovered)) == 0;
            rs_errors += hqc128_fixture_rs_symbol_errors[fixture];
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
