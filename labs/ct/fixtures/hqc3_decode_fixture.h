#ifndef HQC3_DECODE_FIXTURE_H
#define HQC3_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC3_FIXTURE_COUNT 16

extern const uint8_t hqc3_fixture_messages[HQC3_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc3_fixture_codewords[HQC3_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc3_fixture_expected_messages[HQC3_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc3_fixture_rs_symbol_errors[HQC3_FIXTURE_COUNT];

#endif  // HQC3_DECODE_FIXTURE_H
