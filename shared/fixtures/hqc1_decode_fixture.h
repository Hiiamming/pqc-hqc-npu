#ifndef HQC1_DECODE_FIXTURE_H
#define HQC1_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC1_FIXTURE_COUNT 16

extern const uint8_t hqc1_fixture_messages[HQC1_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc1_fixture_codewords[HQC1_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc1_fixture_expected_messages[HQC1_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc1_fixture_rs_symbol_errors[HQC1_FIXTURE_COUNT];

#endif  // HQC1_DECODE_FIXTURE_H
