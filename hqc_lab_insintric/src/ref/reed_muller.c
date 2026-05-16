/**
 * @file reed_muller.c
 * @brief HQC-128 RM(1,7) codec with scalar fallback and Hexagon HVX decode paths.
 *
 * The scalar helpers are kept as a reference-compatible fallback. The intrinsic
 * build accelerates expansion, Hadamard, and peak selection with HVX vectors.
 * Benchmark-only flags can additionally use an RM expansion LUT and a fused
 * full-decode RM block path.
 */

#include "reed_muller.h"
#include <stdint.h>
#include <string.h>
#include "data_structures.h"
#include "parameters.h"

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
#include <hexagon_protos.h>
#include <hexagon_types.h>
#endif

/**
 * @brief Number of repeated 128-bit codeword blocks.
 *
 * Calculates the ceiling of PARAM_N2/128 to determine how many
 * copies of each 128-bit codeword are used in the code expansion.
 */
#define MULTIPLICITY CEIL_DIVIDE(PARAM_N2, 128)

#if (defined(HQC_RM_EXPAND_LUT) || defined(HQC_RM_FUSED_FAST)) && (MULTIPLICITY != 3)
#error "HQC_RM_EXPAND_LUT and HQC_RM_FUSED_FAST currently support the HQC-128 RM multiplicity of 3"
#endif

/**
 * @typedef rm_expanded_cdw
 * @brief Internal representation of a codeword with each bit expanded to a 16-bit signed value.
 */
typedef int16_t rm_expanded_cdw[128];

static inline uint64_t expand_nibble_to_u16(uint32_t x);

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
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

#if defined(HQC_RM_EXPAND_LUT)
static uint64_t rm_expand3_nibble_table[4096] __attribute__((aligned(128)));
static int rm_expand3_nibble_table_ready = 0;

static void init_rm_expand3_nibble_table(void) {
    for (uint32_t n0 = 0; n0 < 16; ++n0) {
        for (uint32_t n1 = 0; n1 < 16; ++n1) {
            for (uint32_t n2 = 0; n2 < 16; ++n2) {
                uint32_t index = n0 | (n1 << 4) | (n2 << 8);
                rm_expand3_nibble_table[index] =
                    expand_nibble_to_u16(n0) + expand_nibble_to_u16(n1) + expand_nibble_to_u16(n2);
            }
        }
    }

    rm_expand3_nibble_table_ready = 1;
}
#endif
#endif

// clang-format off
/**
 * @def BIT0MASK(x)
 * @brief Broadcast the least significant bit of \p x to a 32-bit mask.
 *
 * @param x  An integer expression; only bit 0 is examined.
 * @return   A 32-bit value of all ones (if \p x&1 == 1) or all zeros (if \p x&1 == 0).
 */
#define BIT0MASK(x) (int32_t)(-((x) & 1))
// clang-format on

void encode(rm_codeword_t *word, int32_t message);
void hadamard(rm_expanded_cdw *src, rm_expanded_cdw *dst);
void expand_and_sum(rm_expanded_cdw *dest, rm_codeword_t src[]);
int32_t find_peaks(rm_expanded_cdw *transform);

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst);
void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]);
int32_t find_peaks_hvx(rm_expanded_cdw *transform);
#endif

static inline uint64_t expand_nibble_to_u16(uint32_t x) {
    return ((uint64_t)(x & 1u) << 0) | ((uint64_t)(x & 2u) << 15) |
           ((uint64_t)(x & 4u) << 30) | ((uint64_t)(x & 8u) << 45);
}

/**
 * @brief Encode a single byte into a single codeword using RM(1,7)
 *
 * Encoding matrix of this code:
 * bit pattern (note that bits are numbered big endian)
 * 0   aaaaaaaa aaaaaaaa aaaaaaaa aaaaaaaa
 * 1   cccccccc cccccccc cccccccc cccccccc
 * 2   f0f0f0f0 f0f0f0f0 f0f0f0f0 f0f0f0f0
 * 3   ff00ff00 ff00ff00 ff00ff00 ff00ff00
 * 4   ffff0000 ffff0000 ffff0000 ffff0000
 * 5   ffffffff 00000000 ffffffff 00000000
 * 6   ffffffff ffffffff 00000000 00000000
 * 7   ffffffff ffffffff ffffffff ffffffff
 *
 * @param[out] word An RM(1,7) codeword
 * @param[in] message A message
 */
void encode(rm_codeword_t *word, int32_t message) {
    int32_t first_word;

    first_word = BIT0MASK(message >> 7);

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
    return;
}

/**
 * @brief Scalar RM(1,7) Hadamard transform reference path.
 *
 * Perform the seven RM butterfly passes, alternating between src and dst.
 * The HVX build normally calls hadamard_hvx instead.
 *
 * @param[out] src Structure that contain the expanded codeword
 * @param[out] dst Structure that contain the expanded codeword
 */
void hadamard(rm_expanded_cdw *src, rm_expanded_cdw *dst) {
    // the passes move data:
    // src -> dst -> src -> dst -> src -> dst -> src -> dst
    // using p1 and p2 alternately
    rm_expanded_cdw *p1 = src;
    rm_expanded_cdw *p2 = dst;
    for (int32_t pass = 0; pass < 7; pass++) {
        for (int32_t i = 0; i < 64; i++) {
            (*p2)[i] = (*p1)[2 * i] + (*p1)[2 * i + 1];
            (*p2)[i + 64] = (*p1)[2 * i] - (*p1)[2 * i + 1];
        }
        // swap p1, p2 for next round
        rm_expanded_cdw *p3 = p1;
        p1 = p2;
        p2 = p3;
    }
}

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
/**
 * @brief HVX RM(1,7) Hadamard transform.
 *
 * Each pass loads two 64-lane halfword vectors, uses HVX deal/deinterleave
 * instructions to form even and odd lanes, then computes vector add/sub
 * butterflies. This removes the scalar even/odd gather used by the reference
 * implementation.
 */
void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst) {
    int16_t *p1 = *src;
    int16_t *p2 = *dst;

    for (int32_t pass = 0; pass < 7; pass++) {
        HVX_Vector lo = *(const HVX_Vector *)&p1[0];
        HVX_Vector hi = *(const HVX_Vector *)&p1[64];
        HVX_VectorPair deal = Q6_W_vdeal_VVR(hi, lo, 2);
        HVX_Vector ve = Q6_Vh_vdeal_Vh(Q6_V_lo_W(deal));
        HVX_Vector vo = Q6_Vh_vdeal_Vh(Q6_V_hi_W(deal));
        HVX_Vector sum = Q6_Vh_vadd_VhVh(ve, vo);
        HVX_Vector diff = Q6_Vh_vsub_VhVh(ve, vo);

        *(HVX_Vector *)&p2[0] = sum;
        *(HVX_Vector *)&p2[64] = diff;

        int16_t *p3 = p1;
        p1 = p2;
        p2 = p3;
    }
}
#endif

/**
 * @brief Scalar expansion and summation of repeated RM codeword copies.
 *
 * Accesses memory in order
 * Note: this does not write the codewords as -1 or +1 as the green machine does
 * instead, just 0 and 1 is used.
 * The resulting hadamard transform has:
 * all values are halved
 * the first entry is 64 too high
 *
 * @param[out] dest Structure that contain the expanded codeword
 * @param[in] src Structure that contain the codeword
 */
void expand_and_sum(rm_expanded_cdw *dest, rm_codeword_t src[]) {
    // start with the first copy
    for (int32_t part = 0; part < 4; part++) {
        for (int32_t bit = 0; bit < 32; bit++) {
            (*dest)[part * 32 + bit] = src[0].u32[part] >> bit & 1;
        }
    }
    // sum the rest of the copies
    for (int32_t copy = 1; copy < MULTIPLICITY; copy++) {
        for (int32_t part = 0; part < 4; part++) {
            for (int32_t bit = 0; bit < 32; bit++) {
                (*dest)[part * 32 + bit] += src[copy].u32[part] >> bit & 1;
            }
        }
    }
}

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
/**
 * @brief HVX-friendly packed RM expansion.
 *
 * For HQC-128, MULTIPLICITY is 3. The default intrinsic path expands four
 * bits at a time into packed 16-bit lanes and sums the three repeated copies
 * with integer arithmetic. HQC_RM_EXPAND_LUT=1 replaces the arithmetic with
 * a 4096-entry table from three nibbles to four packed halfword sums.
 */
void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]) {
    uint64_t *out = (uint64_t *)*dest;

#if defined(HQC_RM_EXPAND_LUT)
    if (!rm_expand3_nibble_table_ready) {
        init_rm_expand3_nibble_table();
    }

    for (int32_t part = 0; part < 4; part++) {
        uint32_t w0 = src[0].u32[part];
        uint32_t w1 = src[1].u32[part];
        uint32_t w2 = src[2].u32[part];

        for (int32_t nibble = 0; nibble < 8; nibble++) {
            uint32_t shift = (uint32_t)(4 * nibble);
            uint32_t index = ((w0 >> shift) & 0xfu) |
                             (((w1 >> shift) & 0xfu) << 4) |
                             (((w2 >> shift) & 0xfu) << 8);
            out[part * 8 + nibble] = rm_expand3_nibble_table[index];
        }
    }
#else
    for (int32_t part = 0; part < 4; part++) {
        uint32_t w0 = src[0].u32[part];
        uint32_t w1 = src[1].u32[part];
        uint32_t w2 = src[2].u32[part];

        for (int32_t nibble = 0; nibble < 8; nibble++) {
            uint32_t shift = (uint32_t)(4 * nibble);
            out[part * 8 + nibble] =
                expand_nibble_to_u16((w0 >> shift) & 0xfu) +
                expand_nibble_to_u16((w1 >> shift) & 0xfu) +
                expand_nibble_to_u16((w2 >> shift) & 0xfu);
        }
    }
#endif
}
#endif

/**
 * @brief Scalar peak selection for RM decode.
 *
 * This is the final step of the green machine: find the location of the highest value,
 * and add 128 if the peak is positive
 * if there are two identical peaks, the peak with smallest value
 * in the lowest 7 bits it taken
 * @param[in] transform Structure that contain the expanded codeword
 */
int32_t find_peaks(rm_expanded_cdw *transform) {
    int32_t peak_abs_value = 0;
    int32_t peak_value = 0;
    int32_t peak_pos = 0;
    for (int32_t i = 0; i < 128; i++) {
        // get absolute value
        int32_t t = (*transform)[i];
        int32_t pos_mask = -(t > 0);
        int32_t absolute = (pos_mask & t) | (~pos_mask & -t);
        peak_value = absolute > peak_abs_value ? t : peak_value;
        peak_pos = absolute > peak_abs_value ? i : peak_pos;
        peak_abs_value = absolute > peak_abs_value ? absolute : peak_abs_value;
    }
    // set bit 7
    peak_pos |= 128 * (peak_value > 0);
    return peak_pos;
}

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
/**
 * @brief HVX absolute-maximum reduction with scalar-compatible tie-break.
 *
 * The RM decoder chooses the smallest index with maximal absolute value and
 * sets bit 7 when the signed peak is positive. The HVX path computes absolute
 * values, reduces the maximum, then uses index vectors plus vmin reductions
 * to preserve that scalar tie-break rule.
 */
int32_t find_peaks_hvx(rm_expanded_cdw *transform) {
    HVX_Vector row0 = *(const HVX_Vector *)&(*transform)[0];
    HVX_Vector row1 = *(const HVX_Vector *)&(*transform)[64];
    HVX_Vector abs0 = Q6_Vh_vabs_Vh(row0);
    HVX_Vector abs1 = Q6_Vh_vabs_Vh(row1);
    HVX_Vector max_abs = Q6_Vh_vmax_VhVh(abs0, abs1);

    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 64));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 32));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 16));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 8));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 4));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 2));

    int16_t max_lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)max_lane = max_abs;
    int32_t target_abs_value = max_lane[0];

    HVX_Vector target = Q6_Vh_vsplat_R(target_abs_value);
    HVX_Vector sentinel = Q6_Vh_vsplat_R(0x7fff);
    HVX_Vector idx0 = *(const HVX_Vector *)rm_index_lo;
    HVX_Vector idx1 = *(const HVX_Vector *)rm_index_hi;
    HVX_Vector pos0 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs0, target), idx0, sentinel);
    HVX_Vector pos1 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs1, target), idx1, sentinel);
    HVX_Vector peak_idx = Q6_Vh_vmin_VhVh(pos0, pos1);

    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 64));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 32));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 16));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 8));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 4));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 2));

    int16_t peak_lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)peak_lane = peak_idx;
    int32_t peak_pos = peak_lane[0];
    int32_t peak_value = (*transform)[peak_pos];

    peak_pos |= 128 * (peak_value > 0);
    return peak_pos;
}

#if defined(HQC_RM_FUSED_FAST)
/**
 * @brief Benchmark-only fused RM block decoder.
 *
 * This path is enabled only by HQC_RM_FUSED_FAST on Hexagon. It expands the
 * three RM copies, performs the HVX Hadamard transform, applies the half-
 * Hadamard correction, and runs the HVX peak reduction inside one function.
 * It is used by the fastest full-decode benchmark to reduce handoff overhead;
 * the standalone substage helpers remain available for profiling.
 */
static inline int32_t __attribute__((always_inline)) rm_decode_one_hvx_fast(rm_codeword_t src[]) {
    rm_expanded_cdw expanded __attribute__((aligned(128)));
    rm_expanded_cdw transform __attribute__((aligned(128)));
    uint64_t *out = (uint64_t *)expanded;

#if defined(HQC_RM_EXPAND_LUT)
    if (!rm_expand3_nibble_table_ready) {
        init_rm_expand3_nibble_table();
    }
#endif

    for (int32_t part = 0; part < 4; part++) {
        uint32_t w0 = src[0].u32[part];
        uint32_t w1 = src[1].u32[part];
        uint32_t w2 = src[2].u32[part];

        for (int32_t nibble = 0; nibble < 8; nibble++) {
            uint32_t shift = (uint32_t)(4 * nibble);
#if defined(HQC_RM_EXPAND_LUT)
            uint32_t index = ((w0 >> shift) & 0xfu) |
                             (((w1 >> shift) & 0xfu) << 4) |
                             (((w2 >> shift) & 0xfu) << 8);
            out[part * 8 + nibble] = rm_expand3_nibble_table[index];
#else
            out[part * 8 + nibble] =
                expand_nibble_to_u16((w0 >> shift) & 0xfu) +
                expand_nibble_to_u16((w1 >> shift) & 0xfu) +
                expand_nibble_to_u16((w2 >> shift) & 0xfu);
#endif
        }
    }

    int16_t *p1 = expanded;
    int16_t *p2 = transform;

    for (int32_t pass = 0; pass < 7; pass++) {
        HVX_Vector lo = *(const HVX_Vector *)&p1[0];
        HVX_Vector hi = *(const HVX_Vector *)&p1[64];
        HVX_VectorPair deal = Q6_W_vdeal_VVR(hi, lo, 2);
        HVX_Vector ve = Q6_Vh_vdeal_Vh(Q6_V_lo_W(deal));
        HVX_Vector vo = Q6_Vh_vdeal_Vh(Q6_V_hi_W(deal));
        HVX_Vector sum = Q6_Vh_vadd_VhVh(ve, vo);
        HVX_Vector diff = Q6_Vh_vsub_VhVh(ve, vo);

        *(HVX_Vector *)&p2[0] = sum;
        *(HVX_Vector *)&p2[64] = diff;

        int16_t *p3 = p1;
        p1 = p2;
        p2 = p3;
    }

    p1[0] -= 64 * MULTIPLICITY;

    HVX_Vector row0 = *(const HVX_Vector *)&p1[0];
    HVX_Vector row1 = *(const HVX_Vector *)&p1[64];
    HVX_Vector abs0 = Q6_Vh_vabs_Vh(row0);
    HVX_Vector abs1 = Q6_Vh_vabs_Vh(row1);
    HVX_Vector max_abs = Q6_Vh_vmax_VhVh(abs0, abs1);

    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 64));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 32));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 16));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 8));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 4));
    max_abs = Q6_Vh_vmax_VhVh(max_abs, Q6_V_vror_VR(max_abs, 2));

    int16_t max_lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)max_lane = max_abs;
    int32_t target_abs_value = max_lane[0];

    HVX_Vector target = Q6_Vh_vsplat_R(target_abs_value);
    HVX_Vector sentinel = Q6_Vh_vsplat_R(0x7fff);
    HVX_Vector idx0 = *(const HVX_Vector *)rm_index_lo;
    HVX_Vector idx1 = *(const HVX_Vector *)rm_index_hi;
    HVX_Vector pos0 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs0, target), idx0, sentinel);
    HVX_Vector pos1 = Q6_V_vmux_QVV(Q6_Q_vcmp_eq_VhVh(abs1, target), idx1, sentinel);
    HVX_Vector peak_idx = Q6_Vh_vmin_VhVh(pos0, pos1);

    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 64));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 32));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 16));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 8));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 4));
    peak_idx = Q6_Vh_vmin_VhVh(peak_idx, Q6_V_vror_VR(peak_idx, 2));

    int16_t peak_lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)peak_lane = peak_idx;
    int32_t peak_pos = peak_lane[0];
    int32_t peak_value = p1[peak_pos];

    peak_pos |= 128 * (peak_value > 0);
    return peak_pos;
}
#endif
#endif

/**
 * @brief Encodes the received word
 *
 * The message consists of N1 bytes each byte is encoded into PARAM_N2 bits,
 * or MULTIPLICITY repeats of 128 bits
 *
 * @param[out] cdw Array of size VEC_N1N2_SIZE_64 receiving the encoded message
 * @param[in] msg Array of size VEC_N1_SIZE_64 storing the message
 */
void reed_muller_encode(uint64_t *cdw, const uint64_t *msg) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    for (size_t i = 0; i < VEC_N1_SIZE_BYTES; i++) {
        // fill entries i * MULTIPLICITY to (i+1) * MULTIPLICITY
        int32_t pos = i * MULTIPLICITY;
        // encode first word
        encode(&codeArray[pos], message_array[i]);
        // copy to other identical codewords
        for (size_t copy = 1; copy < MULTIPLICITY; copy++) {
            memcpy(&codeArray[pos + copy], &codeArray[pos], sizeof(rm_codeword_t));
        }
    }
}

/**
 * @brief Decodes the received word
 *
 * The scalar fallback expands each repeated RM block, runs the scalar Hadamard
 * transform, corrects the half-Hadamard offset, and selects the peak. The
 * intrinsic build swaps in HVX expansion, Hadamard, and peak helpers. With
 * HQC_RM_FUSED_FAST=1, the fastest benchmark decodes each RM block through
 * rm_decode_one_hvx_fast instead of calling the helpers separately.
 *
 * For a more complete picture on Reed-Muller decoding, see MacWilliams, Florence
 * Jessie, and Neil James Alexander Sloane. The theory of error-correcting codes codes @cite macwilliams1977theory
 *
 * @param[out] msg Array of size VEC_N1_SIZE_64 receiving the decoded message
 * @param[in] cdw Array of size VEC_N1N2_SIZE_64 storing the received word
 */
void reed_muller_decode(uint64_t *msg, const uint64_t *cdw) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    rm_expanded_cdw expanded __attribute__((aligned(128)));
    for (size_t i = 0; i < VEC_N1_SIZE_BYTES; i++) {
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_RM_FUSED_FAST)
        message_array[i] = rm_decode_one_hvx_fast(&codeArray[i * MULTIPLICITY]);
#else
        // collect the codewords
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
        expand_and_sum_hvx(&expanded, &codeArray[i * MULTIPLICITY]);
#else
        expand_and_sum(&expanded, &codeArray[i * MULTIPLICITY]);
#endif
        // apply hadamard transform
        rm_expanded_cdw transform __attribute__((aligned(128)));
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
        hadamard_hvx(&expanded, &transform);
#else
        hadamard(&expanded, &transform);
#endif
        // fix the first entry to get the half Hadamard transform
        transform[0] -= 64 * MULTIPLICITY;
        // finish the decoding
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS)
        message_array[i] = find_peaks_hvx(&transform);
#else
        message_array[i] = find_peaks(&transform);
#endif
#endif
    }
}
