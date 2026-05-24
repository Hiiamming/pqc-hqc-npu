#ifndef HQC128_DECODE_FIXTURE_H
#define HQC128_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC128_FIXTURE_COUNT 16

extern const uint8_t hqc128_fixture_messages[HQC128_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc128_fixture_codewords[HQC128_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc128_fixture_expected_messages[HQC128_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc128_fixture_rs_symbol_errors[HQC128_FIXTURE_COUNT];

#endif  // HQC128_DECODE_FIXTURE_H
