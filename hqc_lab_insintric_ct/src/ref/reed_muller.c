/**
 * @file reed_muller.c
 * @brief HQC-128 RM(1,7) codec for the Hexagon CT intrinsic path.
 *
 * This directory is intentionally Hexagon-only. The decode path uses fixed
 * arithmetic RM expansion, HVX Hadamard butterflies, and a vector compare/mux
 * peak-sign recovery that avoids secret-indexed loads.
 */

#include "reed_muller.h"
#include <stdint.h>
#include <string.h>
#include "data_structures.h"
#include "parameters.h"

#ifndef __hexagon__
#error "hqc_lab_insintric CT backup is Hexagon-only; use hqc_lab_scalar for host/scalar builds"
#endif

#include <hexagon_protos.h>
#include <hexagon_types.h>

#define MULTIPLICITY CEIL_DIVIDE(PARAM_N2, 128)

#if (MULTIPLICITY != 3) && (MULTIPLICITY != 5)
#error "The CT intrinsic RM path supports the HQC-128 multiplicity of 3 and HQC-192/256 multiplicity of 5"
#endif

typedef int16_t rm_expanded_cdw[128];

static inline uint64_t expand_nibble_to_u16(uint32_t x);
static inline int32_t rm_peak_sign_bit(int32_t peak_value);
static inline void expand_rm_copies_ct(uint64_t *out, const rm_codeword_t src[]);
static inline void rm_hadamard_rows_hvx(HVX_Vector *row0, HVX_Vector *row1);
static inline HVX_Vector hvx_reduce_max_h(HVX_Vector v);
static inline HVX_Vector hvx_reduce_min_h(HVX_Vector v);
static inline HVX_Vector hvx_reduce_xor_h(HVX_Vector v);
static inline int32_t hvx_lane0_i16(HVX_Vector v);
static inline int32_t rm_peak_from_hvx_rows(HVX_Vector row0, HVX_Vector row1);
static inline int32_t __attribute__((always_inline)) rm_decode_one_hvx_ct(rm_codeword_t src[]);

static const int16_t rm_index_lo[64] __attribute__((aligned(128))) = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31,
    32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47,
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63};

static const int16_t rm_index_hi[64] __attribute__((aligned(128))) = {
    64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79,
    80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95,
    96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111,
    112, 113, 114, 115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127};

static const int16_t rm_half_hadamard_fix[64] __attribute__((aligned(128))) = {
    64 * MULTIPLICITY,
};

static inline uint64_t expand_nibble_to_u16(uint32_t x) {
    return ((uint64_t)(x & 1u) << 0) | ((uint64_t)(x & 2u) << 15) |
           ((uint64_t)(x & 4u) << 30) | ((uint64_t)(x & 8u) << 45);
}

static inline int32_t rm_peak_sign_bit(int32_t peak_value) {
    uint32_t peak_u = (uint32_t)peak_value;
    uint32_t peak_nonzero = (peak_u | (uint32_t)-peak_u) >> 31;
    uint32_t peak_negative = peak_u >> 31;
    return (int32_t)(128u * (peak_nonzero & (peak_negative ^ 1u)));
}

static inline void expand_rm_copies_ct(uint64_t *out, const rm_codeword_t src[]) {
#if MULTIPLICITY == 3
#define RM_EXPAND_CT_LOOKUP(part, nibble)                                                        \
    do {                                                                                         \
        uint32_t shift = (uint32_t)(4 * (nibble));                                               \
        out[(part) * 8 + (nibble)] =                                                             \
            expand_nibble_to_u16((w0 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w1 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w2 >> shift) & 0xfu);                                          \
    } while (0)

#define RM_EXPAND_CT_PART(part)                                                                  \
    do {                                                                                         \
        uint32_t w0 = src[0].u32[(part)];                                                        \
        uint32_t w1 = src[1].u32[(part)];                                                        \
        uint32_t w2 = src[2].u32[(part)];                                                        \
        RM_EXPAND_CT_LOOKUP(part, 0);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 1);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 2);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 3);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 4);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 5);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 6);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 7);                                                            \
    } while (0)

    RM_EXPAND_CT_PART(0);
    RM_EXPAND_CT_PART(1);
    RM_EXPAND_CT_PART(2);
    RM_EXPAND_CT_PART(3);

#undef RM_EXPAND_CT_PART
#undef RM_EXPAND_CT_LOOKUP
#elif MULTIPLICITY == 5
#define RM_EXPAND_CT_LOOKUP(part, nibble)                                                        \
    do {                                                                                         \
        uint32_t shift = (uint32_t)(4 * (nibble));                                               \
        out[(part) * 8 + (nibble)] =                                                             \
            expand_nibble_to_u16((w0 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w1 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w2 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w3 >> shift) & 0xfu) +                                         \
            expand_nibble_to_u16((w4 >> shift) & 0xfu);                                          \
    } while (0)

#define RM_EXPAND_CT_PART(part)                                                                  \
    do {                                                                                         \
        uint32_t w0 = src[0].u32[(part)];                                                        \
        uint32_t w1 = src[1].u32[(part)];                                                        \
        uint32_t w2 = src[2].u32[(part)];                                                        \
        uint32_t w3 = src[3].u32[(part)];                                                        \
        uint32_t w4 = src[4].u32[(part)];                                                        \
        RM_EXPAND_CT_LOOKUP(part, 0);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 1);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 2);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 3);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 4);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 5);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 6);                                                            \
        RM_EXPAND_CT_LOOKUP(part, 7);                                                            \
    } while (0)

    RM_EXPAND_CT_PART(0);
    RM_EXPAND_CT_PART(1);
    RM_EXPAND_CT_PART(2);
    RM_EXPAND_CT_PART(3);

#undef RM_EXPAND_CT_PART
#undef RM_EXPAND_CT_LOOKUP
#endif
}

static inline void rm_hadamard_rows_hvx(HVX_Vector *row0, HVX_Vector *row1) {
    HVX_Vector lo = *row0;
    HVX_Vector hi = *row1;

#define RM_HADAMARD_PASS()                                      \
    do {                                                        \
        HVX_VectorPair deal = Q6_W_vdeal_VVR(hi, lo, 2);        \
        HVX_Vector ve = Q6_Vh_vdeal_Vh(Q6_V_lo_W(deal));        \
        HVX_Vector vo = Q6_Vh_vdeal_Vh(Q6_V_hi_W(deal));        \
        lo = Q6_Vh_vadd_VhVh(ve, vo);                           \
        hi = Q6_Vh_vsub_VhVh(ve, vo);                           \
    } while (0)

    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();
    RM_HADAMARD_PASS();

#undef RM_HADAMARD_PASS

    *row0 = lo;
    *row1 = hi;
}

static inline HVX_Vector hvx_reduce_max_h(HVX_Vector v) {
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 64));
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 32));
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 16));
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 8));
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 4));
    v = Q6_Vh_vmax_VhVh(v, Q6_V_vror_VR(v, 2));
    return v;
}

static inline HVX_Vector hvx_reduce_min_h(HVX_Vector v) {
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 64));
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 32));
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 16));
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 8));
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 4));
    v = Q6_Vh_vmin_VhVh(v, Q6_V_vror_VR(v, 2));
    return v;
}

static inline HVX_Vector hvx_reduce_xor_h(HVX_Vector v) {
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 64));
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 32));
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 16));
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 8));
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 4));
    v = Q6_V_vxor_VV(v, Q6_V_vror_VR(v, 2));
    return v;
}

static inline int32_t hvx_lane0_i16(HVX_Vector v) {
    int16_t lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)lane = v;
    return lane[0];
}

static inline int32_t rm_peak_from_hvx_rows(HVX_Vector row0, HVX_Vector row1) {
    HVX_Vector abs0 = Q6_Vh_vabs_Vh(row0);
    HVX_Vector abs1 = Q6_Vh_vabs_Vh(row1);
    HVX_Vector max_abs = hvx_reduce_max_h(Q6_Vh_vmax_VhVh(abs0, abs1));
    int32_t target_abs_value = hvx_lane0_i16(max_abs);

    HVX_Vector target = Q6_Vh_vsplat_R(target_abs_value);
    HVX_Vector sentinel = Q6_Vh_vsplat_R(0x7fff);
    HVX_Vector idx0 = *(const HVX_Vector *)rm_index_lo;
    HVX_Vector idx1 = *(const HVX_Vector *)rm_index_hi;
    HVX_Vector pos0 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs0, target), idx0, sentinel);
    HVX_Vector pos1 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs1, target), idx1, sentinel);
    int32_t peak_pos = hvx_lane0_i16(hvx_reduce_min_h(Q6_Vh_vmin_VhVh(pos0, pos1)));

    HVX_Vector zero = Q6_V_vzero();
    HVX_Vector peak_pos_vec = Q6_Vh_vsplat_R(peak_pos);
    HVX_Vector selected0 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(idx0, peak_pos_vec), row0, zero);
    HVX_Vector selected1 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(idx1, peak_pos_vec), row1, zero);
    int32_t peak_value = hvx_lane0_i16(hvx_reduce_xor_h(Q6_V_vxor_VV(selected0, selected1)));

    return peak_pos | rm_peak_sign_bit(peak_value);
}

#define BIT0MASK(x) (int32_t)(-((x) & 1))

void encode(rm_codeword_t *word, int32_t message) {
    int32_t first_word = BIT0MASK(message >> 7);

    first_word ^= BIT0MASK(message >> 0) & 0xaaaaaaaa;
    first_word ^= BIT0MASK(message >> 1) & 0xcccccccc;
    first_word ^= BIT0MASK(message >> 2) & 0xf0f0f0f0;
    first_word ^= BIT0MASK(message >> 3) & 0xff00ff00;
    first_word ^= BIT0MASK(message >> 4) & 0xffff0000;

    word->u32[0] = first_word;
    first_word ^= BIT0MASK(message >> 5);
    word->u32[1] = first_word;
    first_word ^= BIT0MASK(message >> 6);
    word->u32[3] = first_word;
    first_word ^= BIT0MASK(message >> 5);
    word->u32[2] = first_word;
}

void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]) {
    expand_rm_copies_ct((uint64_t *)*dest, src);
}

void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst) {
    HVX_Vector row0 = *(const HVX_Vector *)&(*src)[0];
    HVX_Vector row1 = *(const HVX_Vector *)&(*src)[64];

    rm_hadamard_rows_hvx(&row0, &row1);

    *(HVX_Vector *)&(*dst)[0] = row0;
    *(HVX_Vector *)&(*dst)[64] = row1;
}

int32_t find_peaks_hvx(rm_expanded_cdw *transform) {
    HVX_Vector row0 = *(const HVX_Vector *)&(*transform)[0];
    HVX_Vector row1 = *(const HVX_Vector *)&(*transform)[64];
    return rm_peak_from_hvx_rows(row0, row1);
}

static inline int32_t __attribute__((always_inline)) rm_decode_one_hvx_ct(rm_codeword_t src[]) {
    rm_expanded_cdw expanded __attribute__((aligned(128)));

    expand_rm_copies_ct((uint64_t *)expanded, src);

    HVX_Vector row0 = *(const HVX_Vector *)&expanded[0];
    HVX_Vector row1 = *(const HVX_Vector *)&expanded[64];
    rm_hadamard_rows_hvx(&row0, &row1);
    row0 = Q6_Vh_vsub_VhVh(row0, *(const HVX_Vector *)rm_half_hadamard_fix);

    return rm_peak_from_hvx_rows(row0, row1);
}

void reed_muller_encode(uint64_t *cdw, const uint64_t *msg) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    for (size_t i = 0; i < VEC_N1_SIZE_BYTES; ++i) {
        int32_t pos = i * MULTIPLICITY;
        encode(&codeArray[pos], message_array[i]);
        for (size_t copy = 1; copy < MULTIPLICITY; ++copy) {
            memcpy(&codeArray[pos + copy], &codeArray[pos], sizeof(rm_codeword_t));
        }
    }
}

void reed_muller_decode(uint64_t *msg, const uint64_t *cdw) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    for (size_t i = 0; i < VEC_N1_SIZE_BYTES; ++i) {
        message_array[i] = rm_decode_one_hvx_ct(&codeArray[i * MULTIPLICITY]);
    }
}
