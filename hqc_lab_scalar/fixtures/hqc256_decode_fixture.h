#ifndef HQC256_DECODE_FIXTURE_H
#define HQC256_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC256_FIXTURE_COUNT 16

extern const uint8_t hqc256_fixture_messages[HQC256_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc256_fixture_codewords[HQC256_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc256_fixture_expected_messages[HQC256_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc256_fixture_rs_symbol_errors[HQC256_FIXTURE_COUNT];

#endif  // HQC256_DECODE_FIXTURE_H
