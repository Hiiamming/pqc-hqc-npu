#ifndef HQC192_DECODE_FIXTURE_H
#define HQC192_DECODE_FIXTURE_H

#include <stdint.h>
#include "parameters.h"

#define HQC192_FIXTURE_COUNT 16

extern const uint8_t hqc192_fixture_messages[HQC192_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc192_fixture_codewords[HQC192_FIXTURE_COUNT][VEC_N1N2_SIZE_BYTES];
extern const uint8_t hqc192_fixture_expected_messages[HQC192_FIXTURE_COUNT][PARAM_K];
extern const uint8_t hqc192_fixture_rs_symbol_errors[HQC192_FIXTURE_COUNT];

#endif  // HQC192_DECODE_FIXTURE_H
