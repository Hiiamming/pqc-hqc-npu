#ifndef HQC5_DECODE_FIXTURE_H
#define HQC5_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC5_FIXTURE_COUNT 256

extern const uint8_t hqc5_fixture_messages[HQC5_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc5_fixture_codewords[HQC5_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc5_fixture_expected_messages[HQC5_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc5_fixture_rs_symbol_errors[HQC5_FIXTURE_COUNT];

#endif  // HQC5_DECODE_FIXTURE_H
