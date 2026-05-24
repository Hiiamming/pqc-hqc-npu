#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "code.h"
#include "parameters.h"

#define RM_BLOCK_BYTES (PARAM_N2 / 8)
#define MSG_WORDS CEIL_DIVIDE(PARAM_K, 8)

#ifndef HQC_MODE
#define HQC_MODE 128
#endif

static void dump_hex(const char *label, const uint8_t *buf, size_t len)
{
    printf("%s", label);
    for (size_t i = 0; i < len; ++i) {
        printf("%02x", buf[i]);
    }
    printf("\n");
}

static void inject_rs_symbol_errors(uint64_t *em, size_t errors)
{
    uint8_t *bytes = (uint8_t *)em;

    for (size_t i = 0; i < errors; ++i) {
        size_t position = (i * 3u + 1u) % PARAM_N1;
        size_t offset = position * RM_BLOCK_BYTES;
        bytes[offset] ^= 0xffu;
    }
}

int main(void)
{
    uint8_t message[PARAM_K] = {0};
    uint64_t msg_words[MSG_WORDS] = {0};
    uint64_t recovered_words[MSG_WORDS] = {0};
    uint64_t em[VEC_N1N2_SIZE_64] = {0};

    for (size_t i = 0; i < PARAM_K; ++i) {
        message[i] = (uint8_t)i;
    }

    memcpy(msg_words, message, sizeof(message));

    printf("HQC-%d concatenated codec demo\n", HQC_MODE);
    printf("PARAM_N1=%d PARAM_N2=%d PARAM_K=%d PARAM_DELTA=%d\n",
           PARAM_N1,
           PARAM_N2,
           PARAM_K,
           PARAM_DELTA);
    dump_hex("message:   ", message, sizeof(message));

    code_encode(em, msg_words);
    inject_rs_symbol_errors(em, 8);
    code_decode(recovered_words, em);

    dump_hex("recovered: ", (const uint8_t *)recovered_words, PARAM_K);

    int ok = memcmp(message, recovered_words, sizeof(message)) == 0;
    printf("roundtrip_with_8_symbol_errors: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
