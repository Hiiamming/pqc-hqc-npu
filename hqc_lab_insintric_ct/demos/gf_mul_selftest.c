#include <stdint.h>
#include <stdio.h>

#include "gf.h"
#include "parameters.h"

static uint16_t gf_mul_lut_reference(uint16_t a, uint16_t b) {
    if (a == 0 || b == 0) {
        return 0;
    }

    uint16_t index = gf_log[a] + gf_log[b];
    if (index >= PARAM_GF_MUL_ORDER) {
        index -= PARAM_GF_MUL_ORDER;
    }

    return gf_exp[index];
}

int main(void) {
    for (uint16_t a = 0; a < 256; ++a) {
        for (uint16_t b = 0; b < 256; ++b) {
            uint16_t got = gf_mul(a, b);
            uint16_t want = gf_mul_lut_reference(a, b);
            if (got != want) {
                printf("FAIL a=%u b=%u got=%u want=%u\n", a, b, got, want);
                return 1;
            }
        }
    }

    puts("PASS gf_mul selftest 256x256");
    return 0;
}
