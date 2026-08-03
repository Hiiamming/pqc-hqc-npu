/**
 * @file reed_muller.c
 * @brief HQC-128 RM(1,7) codec for the experimental HMX-32/HVX-tail path.
 *
 * The optional HMX baseline batches 32 transforms into a matrix multiply and
 * leaves remaining blocks on the fused HVX expansion/Hadamard/peak path.
 */

#include "reed_muller.h"
#include <stdint.h>
#include <string.h>
#include "data_structures.h"
#include "parameters.h"

#ifndef HQC_RM_HMX_DEVICE
#define HQC_RM_HMX_DEVICE 0
#endif

#ifndef HQC_RM_HMX_BATCH
#define HQC_RM_HMX_BATCH 0
#endif

#if HQC_RM_HMX_BATCH && HQC_RM_HMX_DEVICE
#include "HAP_compute_res.h"
#include "HAP_power.h"
#include "hexkl_micro.h"
#elif HQC_RM_HMX_BATCH
#include <h2.h>
#endif

#ifndef __hexagon__
#error "hqc_lab_insintric is Hexagon-only; use labs/scalar for portable scalar builds"
#endif

#include <hexagon_protos.h>
#include <hexagon_types.h>
#include <hmx_hexagon_protos.h>

/* Configuration, types, and lookup tables. */

/**
 * @brief Number of repeated 128-bit codeword blocks.
 *
 * Calculates the ceiling of PARAM_N2/128 to determine how many
 * copies of each 128-bit codeword are used in the code expansion.
 */
#define MULTIPLICITY CEIL_DIVIDE(PARAM_N2, 128)

#if (MULTIPLICITY != 3) && (MULTIPLICITY != 5)
#error "The fastest RM path supports the HQC-128 multiplicity of 3 and HQC-192/256 multiplicity of 5"
#endif

/**
 * @typedef rm_expanded_cdw
 * @brief Internal representation of a codeword with each bit expanded to a 16-bit signed value.
 */
typedef int16_t rm_expanded_cdw[128];

static inline uint64_t expand_nibble_to_u16(uint32_t x);
static inline int32_t rm_peak_sign_bit(int32_t peak_value);

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

#if (MULTIPLICITY == 3) || (MULTIPLICITY == 5)
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

#if MULTIPLICITY == 5
static uint64_t rm_expand2_nibble_table[256] __attribute__((aligned(128)));
static int rm_expand2_nibble_table_ready = 0;

static void init_rm_expand2_nibble_table(void) {
    for (uint32_t n0 = 0; n0 < 16; ++n0) {
        for (uint32_t n1 = 0; n1 < 16; ++n1) {
            uint32_t index = n0 | (n1 << 4);
            rm_expand2_nibble_table[index] = expand_nibble_to_u16(n0) + expand_nibble_to_u16(n1);
        }
    }

    rm_expand2_nibble_table_ready = 1;
}
#endif

static inline void rm_expand_copies_to_halfwords(uint64_t *out, const rm_codeword_t src[]);
static inline void rm_apply_hadamard_rows_hvx(HVX_Vector *row0, HVX_Vector *row1);
static inline void rm_apply_hadamard_two_blocks_hvx(HVX_Vector *a0, HVX_Vector *a1, HVX_Vector *b0, HVX_Vector *b1);
#if HQC_RM_HMX_BATCH
static int rm_hmx_init(void);
static inline uint16_t rm_i16_to_f16_bits(int16_t x);
static inline int16_t rm_f16_bits_to_i16(uint16_t x);
static void rm_hadamard_32_hmx_from_expanded(const rm_expanded_cdw src[32], rm_expanded_cdw dst[32]);
static int rm_decode_32_blocks_hmx(uint8_t *message_array, rm_codeword_t *codeArray, size_t first_block);
#endif
static inline HVX_Vector hvx_reduce_max_h(HVX_Vector v);
static inline HVX_Vector hvx_reduce_min_h(HVX_Vector v);
static inline int32_t hvx_lane0_i16(HVX_Vector v);
static inline int32_t rm_select_peak_from_rows(HVX_Vector row0, HVX_Vector row1);
static inline int32_t rm_decode_one_block_hvx(rm_codeword_t src[]);
static inline void rm_decode_two_blocks_hvx(uint8_t *message_array, rm_codeword_t *codeArray, size_t i);

static const int16_t rm_half_hadamard_fix[64] __attribute__((aligned(128))) = {
    64 * MULTIPLICITY,
};

#if HQC_RM_HMX_BATCH
#define RM_HMX_BATCH_BLOCKS 32
#define RM_HMX_TILE 32
#define RM_HMX_TILES 4
#define RM_HMX_ACT_CROUTON_HALFWORDS 1024
#define RM_HMX_ACT_CROUTON_BYTES 2048u
#define RM_HMX_ACT_OFFSET 0u
#define RM_HMX_WGT_OFFSET 0x2000u
#define RM_HMX_OUT_OFFSET 0xa000u
#define RM_HMX_SCALE_OFFSET 0xb000u
#define RM_HMX_SCALE_BYTES 256u
#define RM_HMX_VTCM_BYTES (RM_HMX_SCALE_OFFSET + RM_HMX_SCALE_BYTES)

#ifndef HQC_RM_HMX_OUTPUT_BIAS
#if HQC_RM_HMX_DEVICE
#define HQC_RM_HMX_OUTPUT_BIAS 0
#else
#define HQC_RM_HMX_OUTPUT_BIAS 1
#endif
#endif

#define rm_hmx_activation ((uint16_t *)(rm_hmx_vtcm_base + RM_HMX_ACT_OFFSET))
#define rm_hmx_weights ((uint16_t *)(rm_hmx_vtcm_base + RM_HMX_WGT_OFFSET))
#define rm_hmx_output ((uint16_t *)(rm_hmx_vtcm_base + RM_HMX_OUT_OFFSET))
#define rm_hmx_scales ((uint16_t *)(rm_hmx_vtcm_base + RM_HMX_SCALE_OFFSET))
static int rm_hmx_ready = 0;
static uint8_t *rm_hmx_vtcm_base;
#if HQC_RM_HMX_DEVICE
static int rm_hmx_power_ctx;
static unsigned int rm_hmx_context_id;
static uint32_t rm_hmx_config_offset;
#else
static h2_vecaccess_state_t rm_hmx_vacc;
static h2_mxaccess_state_t rm_hmx_mxacc;
static int rm_hmx_vacc_idx = -1;
static int rm_hmx_mxacc_idx = -1;
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
void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst);
#if HQC_RM_HMX_BATCH
void hadamard_hmx32(rm_expanded_cdw src[32], rm_expanded_cdw dst[32]);
#endif
void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]);
int32_t find_peaks_hvx(rm_expanded_cdw *transform);

/* RM HVX primitives. */

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

#if HQC_RM_HMX_BATCH
static inline uint16_t rm_i16_to_f16_bits(int16_t x) {
    __fp16 h = (__fp16)x;
    uint16_t bits;
    memcpy(&bits, &h, sizeof(bits));
    return bits;
}

static inline int16_t rm_f16_bits_to_i16(uint16_t x) {
    __fp16 h;
    memcpy(&h, &x, sizeof(h));
    return (int16_t)h;
}

static inline int16_t rm_hadamard_sign(size_t row, size_t col) {
    return (__builtin_popcount((unsigned)(row & col)) & 1) ? -1 : 1;
}

#if HQC_RM_HMX_DEVICE
static int rm_hmx_power_on(void) {
    HAP_power_request_t req;

    memset(&req, 0, sizeof(req));
    req.type = HAP_power_set_apptype;
    req.apptype = HAP_POWER_COMPUTE_CLIENT_CLASS;
    if (HAP_power_set((void *)&rm_hmx_power_ctx, &req) != 0) {
        return -1;
    }

    memset(&req, 0, sizeof(req));
    req.type = HAP_power_set_DCVS_v3;
    req.dcvs_v3.set_dcvs_enable = 1;
    req.dcvs_v3.dcvs_enable = 1;
    req.dcvs_v3.dcvs_option = HAP_DCVS_V2_PERFORMANCE_MODE;
    req.dcvs_v3.set_bus_params = 1;
    req.dcvs_v3.bus_params.min_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.bus_params.max_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.bus_params.target_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.set_core_params = 1;
    req.dcvs_v3.core_params.min_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.core_params.max_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.core_params.target_corner = HAP_DCVS_VCORNER_MAX;
    req.dcvs_v3.set_sleep_disable = 1;
    req.dcvs_v3.sleep_disable = 1;
    if (HAP_power_set((void *)&rm_hmx_power_ctx, &req) != 0) {
        return -2;
    }

    memset(&req, 0, sizeof(req));
    req.type = HAP_power_set_HVX;
    req.hvx.power_up = 1;
    if (HAP_power_set((void *)&rm_hmx_power_ctx, &req) != 0) {
        return -3;
    }

    memset(&req, 0, sizeof(req));
    req.type = HAP_power_set_HMX;
    req.hmx.power_up = 1;
    if (HAP_power_set((void *)&rm_hmx_power_ctx, &req) != 0) {
        return -4;
    }

    return 0;
}

static void rm_hmx_release_device(void) {
    if (rm_hmx_context_id != 0) {
        HAP_compute_res_hmx_unlock(rm_hmx_context_id);
        HAP_compute_res_release(rm_hmx_context_id);
        rm_hmx_context_id = 0;
    }
    HAP_power_destroy((void *)&rm_hmx_power_ctx);
    rm_hmx_vtcm_base = 0;
    rm_hmx_ready = 0;
}
#endif

static int rm_hmx_init(void) {
    if (rm_hmx_ready != 0) {
        return rm_hmx_ready > 0 ? 0 : -1;
    }

#if HQC_RM_HMX_DEVICE
    compute_res_attr_t attr;
    uint32_t vtcm_bytes = RM_HMX_VTCM_BYTES + hexkl_micro_hmx_config_size();

    if (rm_hmx_power_on() != 0 ||
        HAP_compute_res_attr_init(&attr) != 0 ||
        HAP_compute_res_attr_set_vtcm_param(&attr, vtcm_bytes, 1) != 0 ||
        HAP_compute_res_attr_set_hmx_param(&attr, 1) != 0) {
        rm_hmx_release_device();
        return -1;
    }

    rm_hmx_context_id = HAP_compute_res_acquire(&attr, 100000);
    if (rm_hmx_context_id == 0) {
        rm_hmx_release_device();
        return -1;
    }

    rm_hmx_vtcm_base = HAP_compute_res_attr_get_vtcm_ptr(&attr);
    if (rm_hmx_vtcm_base == 0 ||
        HAP_compute_res_hmx_lock(rm_hmx_context_id) != 0) {
        rm_hmx_release_device();
        return -1;
    }

    rm_hmx_config_offset = RM_HMX_VTCM_BYTES;
    if (hexkl_micro_hmx_setup_acc_read_f16(rm_hmx_vtcm_base, rm_hmx_config_offset) != 0) {
        rm_hmx_release_device();
        return -1;
    }
#else
    uintptr_t vtcm_base = (uintptr_t)h2_info(INFO_VTCM_BASE);
    uint32_t vtcm_size = (uint32_t)h2_info(INFO_VTCM_SIZE) * 1024u;
    if (vtcm_base == 0 || vtcm_size < RM_HMX_VTCM_BYTES) {
        rm_hmx_ready = -1;
        return -1;
    }

    if (h2_vecaccess_unit_init(&rm_hmx_vacc,
                               H2_VECACCESS_HVX_128,
                               CFG_TYPE_VXU0,
                               CFG_SUBTYPE_VXU0,
                               CFG_HVX_CONTEXTS,
                               0x1) < 0) {
        rm_hmx_ready = -1;
        return -1;
    }
    h2_vecaccess_ret_t vret = h2_vecaccess_acquire(&rm_hmx_vacc);
    if (vret.idx < 0) {
        rm_hmx_ready = -1;
        return -1;
    }
    rm_hmx_vacc_idx = vret.idx;

    if (h2_mxaccess_unit_init(&rm_hmx_mxacc,
                              CFG_TYPE_VXU0,
                              CFG_SUBTYPE_VXU0,
                              CFG_HMX_CONTEXTS,
                              0x1) < 0) {
        h2_vecaccess_release(&rm_hmx_vacc, rm_hmx_vacc_idx);
        rm_hmx_vacc_idx = -1;
        rm_hmx_ready = -1;
        return -1;
    }
    rm_hmx_mxacc_idx = h2_mxaccess_acquire(&rm_hmx_mxacc);
    if (rm_hmx_mxacc_idx < 0) {
        h2_vecaccess_release(&rm_hmx_vacc, rm_hmx_vacc_idx);
        rm_hmx_vacc_idx = -1;
        rm_hmx_ready = -1;
        return -1;
    }

    rm_hmx_vtcm_base = (uint8_t *)vtcm_base;
#endif

    for (size_t row_tile = 0; row_tile < RM_HMX_TILES; ++row_tile) {
        for (size_t k_tile = 0; k_tile < RM_HMX_TILES; ++k_tile) {
            uint16_t *base = rm_hmx_weights +
                             (row_tile * RM_HMX_TILES + k_tile) * RM_HMX_ACT_CROUTON_HALFWORDS;
            uint16_t *p = base;
            for (size_t vec = 0; vec < RM_HMX_TILE / 2; ++vec) {
                for (size_t out = 0; out < RM_HMX_TILE; ++out) {
                    for (size_t in_pair = 0; in_pair < 2; ++in_pair) {
                        size_t row = row_tile * RM_HMX_TILE + out;
                        size_t col = k_tile * RM_HMX_TILE + vec * 2 + in_pair;
                        *p++ = rm_i16_to_f16_bits(rm_hadamard_sign(row, col));
                    }
                }
            }
        }
    }

#if HQC_RM_HMX_DEVICE
    (void)rm_hmx_scales;
#else
    *(HVX_Vector *)&rm_hmx_scales[0] = Q6_V_vsplat_R(0x3c003c00);
    *(HVX_Vector *)&rm_hmx_scales[64] = Q6_V_vzero();
    Q6_bias_mxmem2_A(rm_hmx_scales);
#endif

    rm_hmx_ready = 1;
    return 0;
}

int reed_muller_hmx_device_acquire(void) {
#if HQC_RM_HMX_BATCH && HQC_RM_HMX_DEVICE
    return rm_hmx_init();
#else
    return 0;
#endif
}

void reed_muller_hmx_device_release(void) {
#if HQC_RM_HMX_BATCH && HQC_RM_HMX_DEVICE
    rm_hmx_release_device();
#endif
}

static void rm_hmx_pack_activation_from_expanded(const rm_expanded_cdw src[RM_HMX_BATCH_BLOCKS]) {
    for (size_t block = 0; block < RM_HMX_BATCH_BLOCKS; ++block) {
        size_t vec = block >> 1;
        size_t sp_in_vec = block & 1;
        for (size_t k_tile = 0; k_tile < RM_HMX_TILES; ++k_tile) {
            uint16_t *base = rm_hmx_activation + k_tile * RM_HMX_ACT_CROUTON_HALFWORDS + vec * 64 + sp_in_vec;
            for (size_t ch = 0; ch < RM_HMX_TILE; ++ch) {
                base[ch * 2] = rm_i16_to_f16_bits(src[block][k_tile * RM_HMX_TILE + ch]);
            }
        }
    }
}

static void rm_hmx_pack_activation_from_codewords(rm_codeword_t *codeArray) {
    rm_expanded_cdw expanded __attribute__((aligned(128)));
    for (size_t block = 0; block < RM_HMX_BATCH_BLOCKS; ++block) {
        rm_expand_copies_to_halfwords((uint64_t *)expanded, &codeArray[block * MULTIPLICITY]);
        size_t vec = block >> 1;
        size_t sp_in_vec = block & 1;
        for (size_t k_tile = 0; k_tile < RM_HMX_TILES; ++k_tile) {
            uint16_t *base = rm_hmx_activation + k_tile * RM_HMX_ACT_CROUTON_HALFWORDS + vec * 64 + sp_in_vec;
            for (size_t ch = 0; ch < RM_HMX_TILE; ++ch) {
                base[ch * 2] = rm_i16_to_f16_bits(expanded[k_tile * RM_HMX_TILE + ch]);
            }
        }
    }
}

static inline __attribute__((always_inline)) void rm_hmx_load_tile_pair(const void *activation, const void *weights) {
    uint32_t limit = RM_HMX_ACT_CROUTON_BYTES - 1;
    asm volatile(
        "{ activation.hf = mxmem(%0, %1)\n"
        "  weight.hf = mxmem(%2, %3) }\n"
        :: "r"(activation), "r"(limit), "r"(weights), "r"(limit)
        : "memory");
}

static inline __attribute__((always_inline)) void rm_hmx_store_output(void *output) {
    asm volatile(
        "cvt.hf = acc(%0)\n"
        "mxmem(%1, %2) = cvt\n"
        :: "r"(2), "r"(output), "r"(0)
        : "memory");
}

static void rm_hmx_run_row_tile(size_t row_tile) {
    const uint16_t *weights = rm_hmx_weights +
                              row_tile * RM_HMX_TILES * RM_HMX_ACT_CROUTON_HALFWORDS;

    Q6_mxclracc_hf();
    for (size_t k_tile = 0; k_tile < RM_HMX_TILES; ++k_tile) {
        rm_hmx_load_tile_pair(rm_hmx_activation + k_tile * RM_HMX_ACT_CROUTON_HALFWORDS,
                              weights + k_tile * RM_HMX_ACT_CROUTON_HALFWORDS);
    }
    rm_hmx_store_output(rm_hmx_output);
}

static void rm_hmx_unpack_row_tile(size_t row_tile, rm_expanded_cdw dst[RM_HMX_BATCH_BLOCKS]) {
    const uint16_t *p = rm_hmx_output;
    for (size_t vec = 0; vec < RM_HMX_TILE / 2; ++vec) {
        for (size_t out = 0; out < RM_HMX_TILE; ++out) {
            for (size_t sp_in_vec = 0; sp_in_vec < 2; ++sp_in_vec) {
                size_t block = vec * 2 + sp_in_vec;
                /* H2 simulator and hardware direct readback use different bias. */
                dst[block][row_tile * RM_HMX_TILE + out] =
                    rm_f16_bits_to_i16(*p++) - HQC_RM_HMX_OUTPUT_BIAS;
            }
        }
    }
}

static void rm_hadamard_32_hmx_from_expanded(const rm_expanded_cdw src[RM_HMX_BATCH_BLOCKS], rm_expanded_cdw dst[RM_HMX_BATCH_BLOCKS]) {
    if (rm_hmx_init() < 0) {
        for (size_t block = 0; block < RM_HMX_BATCH_BLOCKS; ++block) {
            hadamard_hvx((rm_expanded_cdw *)&src[block], &dst[block]);
        }
        return;
    }
    rm_hmx_pack_activation_from_expanded(src);
    for (size_t row_tile = 0; row_tile < RM_HMX_TILES; ++row_tile) {
        rm_hmx_run_row_tile(row_tile);
        rm_hmx_unpack_row_tile(row_tile, dst);
    }
}

void hadamard_hmx32(rm_expanded_cdw src[32], rm_expanded_cdw dst[32]) {
    rm_hadamard_32_hmx_from_expanded((const rm_expanded_cdw *)src, dst);
}
#endif

static inline void rm_expand_copies_to_halfwords(uint64_t *out, const rm_codeword_t src[]) {
#if MULTIPLICITY == 3
    if (!rm_expand3_nibble_table_ready) {
        init_rm_expand3_nibble_table();
    }

#define RM_EXPAND3_PART(part)                                                                      \
        do {                                                                                       \
            uint32_t w0 = src[0].u32[(part)];                                                      \
            uint32_t w1 = src[1].u32[(part)];                                                      \
            uint32_t w2 = src[2].u32[(part)];                                                      \
            RM_EXPAND3_LOOKUP(part, 0);                                                            \
            RM_EXPAND3_LOOKUP(part, 1);                                                            \
            RM_EXPAND3_LOOKUP(part, 2);                                                            \
            RM_EXPAND3_LOOKUP(part, 3);                                                            \
            RM_EXPAND3_LOOKUP(part, 4);                                                            \
            RM_EXPAND3_LOOKUP(part, 5);                                                            \
            RM_EXPAND3_LOOKUP(part, 6);                                                            \
            RM_EXPAND3_LOOKUP(part, 7);                                                            \
        } while (0)

#define RM_EXPAND3_LOOKUP(part, nibble)                                                           \
        do {                                                                                       \
            uint32_t shift = (uint32_t)(4 * (nibble));                                             \
            uint32_t index = ((w0 >> shift) & 0xfu) | (((w1 >> shift) & 0xfu) << 4) |              \
                             (((w2 >> shift) & 0xfu) << 8);                                        \
            out[(part) * 8 + (nibble)] = rm_expand3_nibble_table[index];                           \
        } while (0)

    RM_EXPAND3_PART(0);
    RM_EXPAND3_PART(1);
    RM_EXPAND3_PART(2);
    RM_EXPAND3_PART(3);

#undef RM_EXPAND3_PART
#undef RM_EXPAND3_LOOKUP
#elif MULTIPLICITY == 5
    if (!rm_expand3_nibble_table_ready) {
        init_rm_expand3_nibble_table();
    }
    if (!rm_expand2_nibble_table_ready) {
        init_rm_expand2_nibble_table();
    }

#define RM_EXPAND5_PART(part)                                                                      \
        do {                                                                                       \
            uint32_t w0 = src[0].u32[(part)];                                                      \
            uint32_t w1 = src[1].u32[(part)];                                                      \
            uint32_t w2 = src[2].u32[(part)];                                                      \
            uint32_t w3 = src[3].u32[(part)];                                                      \
            uint32_t w4 = src[4].u32[(part)];                                                      \
            RM_EXPAND5_LOOKUP(part, 0);                                                            \
            RM_EXPAND5_LOOKUP(part, 1);                                                            \
            RM_EXPAND5_LOOKUP(part, 2);                                                            \
            RM_EXPAND5_LOOKUP(part, 3);                                                            \
            RM_EXPAND5_LOOKUP(part, 4);                                                            \
            RM_EXPAND5_LOOKUP(part, 5);                                                            \
            RM_EXPAND5_LOOKUP(part, 6);                                                            \
            RM_EXPAND5_LOOKUP(part, 7);                                                            \
        } while (0)

#define RM_EXPAND5_LOOKUP(part, nibble)                                                           \
        do {                                                                                       \
            uint32_t shift = (uint32_t)(4 * (nibble));                                             \
            uint32_t index3 = ((w0 >> shift) & 0xfu) | (((w1 >> shift) & 0xfu) << 4) |             \
                              (((w2 >> shift) & 0xfu) << 8);                                       \
            uint32_t index2 = ((w3 >> shift) & 0xfu) | (((w4 >> shift) & 0xfu) << 4);              \
            out[(part) * 8 + (nibble)] = rm_expand3_nibble_table[index3] +                         \
                                         rm_expand2_nibble_table[index2];                          \
        } while (0)

    RM_EXPAND5_PART(0);
    RM_EXPAND5_PART(1);
    RM_EXPAND5_PART(2);
    RM_EXPAND5_PART(3);

#undef RM_EXPAND5_PART
#undef RM_EXPAND5_LOOKUP
#else
    for (int32_t part = 0; part < 4; part++) {
        uint32_t words[MULTIPLICITY];
        for (int32_t copy = 0; copy < MULTIPLICITY; copy++) {
            words[copy] = src[copy].u32[part];
        }

        for (int32_t nibble = 0; nibble < 8; nibble++) {
            uint32_t shift = (uint32_t)(4 * nibble);
            uint64_t acc = 0;
            for (int32_t copy = 0; copy < MULTIPLICITY; copy++) {
                acc += expand_nibble_to_u16((words[copy] >> shift) & 0xfu);
            }
            out[part * 8 + nibble] = acc;
        }
    }
#endif
}

static inline void rm_apply_hadamard_rows_hvx(HVX_Vector *row0, HVX_Vector *row1) {
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

static inline void rm_apply_hadamard_two_blocks_hvx(HVX_Vector *a0, HVX_Vector *a1, HVX_Vector *b0, HVX_Vector *b1) {
    HVX_Vector alo = *a0;
    HVX_Vector ahi = *a1;
    HVX_Vector blo = *b0;
    HVX_Vector bhi = *b1;

#define RM_HADAMARD_TWO_PASS()                                  \
    do {                                                        \
        HVX_VectorPair adeal = Q6_W_vdeal_VVR(ahi, alo, 2);     \
        HVX_VectorPair bdeal = Q6_W_vdeal_VVR(bhi, blo, 2);     \
        HVX_Vector ae = Q6_Vh_vdeal_Vh(Q6_V_lo_W(adeal));       \
        HVX_Vector be = Q6_Vh_vdeal_Vh(Q6_V_lo_W(bdeal));       \
        HVX_Vector ao = Q6_Vh_vdeal_Vh(Q6_V_hi_W(adeal));       \
        HVX_Vector bo = Q6_Vh_vdeal_Vh(Q6_V_hi_W(bdeal));       \
        alo = Q6_Vh_vadd_VhVh(ae, ao);                          \
        blo = Q6_Vh_vadd_VhVh(be, bo);                          \
        ahi = Q6_Vh_vsub_VhVh(ae, ao);                          \
        bhi = Q6_Vh_vsub_VhVh(be, bo);                          \
    } while (0)

    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();
    RM_HADAMARD_TWO_PASS();

#undef RM_HADAMARD_TWO_PASS

    *a0 = alo;
    *a1 = ahi;
    *b0 = blo;
    *b1 = bhi;
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

static inline int32_t hvx_lane0_i16(HVX_Vector v) {
    int16_t lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)lane = v;
    return lane[0];
}

static inline int32_t rm_select_peak_from_rows(HVX_Vector row0, HVX_Vector row1) {
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

    int16_t rows[128] __attribute__((aligned(128)));
    *(HVX_Vector *)&rows[0] = row0;
    *(HVX_Vector *)&rows[64] = row1;
    int32_t peak_value = rows[peak_pos];

    return peak_pos | rm_peak_sign_bit(peak_value);
}

/* Public RM encode helper. */

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

/* Substage benchmark adapters. */

/**
 * @brief HVX RM(1,7) Hadamard transform.
 *
 * The transform loads two 64-lane halfword vectors once, keeps them in HVX
 * registers across the seven deal/deinterleave butterfly passes, then stores
 * the final rows. This removes both the scalar even/odd gather and the old
 * per-pass stack ping-pong.
 */
void hadamard_hvx(rm_expanded_cdw *src, rm_expanded_cdw *dst) {
    HVX_Vector row0 = *(const HVX_Vector *)&(*src)[0];
    HVX_Vector row1 = *(const HVX_Vector *)&(*src)[64];

    rm_apply_hadamard_rows_hvx(&row0, &row1);

    *(HVX_Vector *)&(*dst)[0] = row0;
    *(HVX_Vector *)&(*dst)[64] = row1;
}

/**
 * @brief HVX-friendly packed RM expansion.
 *
 * HQC-128 uses the measured fastest 3-copy LUT expansion. HQC-192/256 use
 * the same packed halfword representation with arithmetic summation of 5
 * repeated copies.
 */
void expand_and_sum_hvx(rm_expanded_cdw *dest, rm_codeword_t src[]) {
    rm_expand_copies_to_halfwords((uint64_t *)*dest, src);
}

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
    return rm_select_peak_from_rows(row0, row1);
}

/* Fused fastest decode path. */

/**
 * @brief Fused RM block decoder used by the only active intrinsic decode path.
 *
 * It expands the repeated RM copies, performs the HVX Hadamard transform, applies
 * the half-Hadamard correction, and runs the HVX peak reduction inside one
 * function. The standalone helpers remain available for substage profiling.
 */
static inline int32_t __attribute__((always_inline)) rm_decode_one_block_hvx(rm_codeword_t src[]) {
    rm_expanded_cdw expanded __attribute__((aligned(128)));

    rm_expand_copies_to_halfwords((uint64_t *)expanded, src);

    HVX_Vector row0 = *(const HVX_Vector *)&expanded[0];
    HVX_Vector row1 = *(const HVX_Vector *)&expanded[64];
    rm_apply_hadamard_rows_hvx(&row0, &row1);
    row0 = Q6_Vh_vsub_VhVh(row0, *(const HVX_Vector *)rm_half_hadamard_fix);

    return rm_select_peak_from_rows(row0, row1);
}

static inline void __attribute__((always_inline)) rm_decode_two_blocks_hvx(uint8_t *message_array, rm_codeword_t *codeArray, size_t i) {
    rm_expanded_cdw expanded0 __attribute__((aligned(128)));
    rm_expanded_cdw expanded1 __attribute__((aligned(128)));

    rm_expand_copies_to_halfwords((uint64_t *)expanded0, &codeArray[i * MULTIPLICITY]);
    rm_expand_copies_to_halfwords((uint64_t *)expanded1, &codeArray[(i + 1) * MULTIPLICITY]);

    HVX_Vector row00 = *(const HVX_Vector *)&expanded0[0];
    HVX_Vector row01 = *(const HVX_Vector *)&expanded0[64];
    HVX_Vector row10 = *(const HVX_Vector *)&expanded1[0];
    HVX_Vector row11 = *(const HVX_Vector *)&expanded1[64];
    rm_apply_hadamard_two_blocks_hvx(&row00, &row01, &row10, &row11);

    HVX_Vector fix = *(const HVX_Vector *)rm_half_hadamard_fix;
    row00 = Q6_Vh_vsub_VhVh(row00, fix);
    row10 = Q6_Vh_vsub_VhVh(row10, fix);

    message_array[i] = rm_select_peak_from_rows(row00, row01);
    message_array[i + 1] = rm_select_peak_from_rows(row10, row11);
}

#if HQC_RM_HMX_BATCH
static int rm_decode_32_blocks_hmx(uint8_t *message_array, rm_codeword_t *codeArray, size_t first_block) {
    rm_expanded_cdw transform[RM_HMX_BATCH_BLOCKS] __attribute__((aligned(128)));

    if (rm_hmx_init() < 0) {
        return -1;
    }
    rm_hmx_pack_activation_from_codewords(&codeArray[first_block * MULTIPLICITY]);
    for (size_t row_tile = 0; row_tile < RM_HMX_TILES; ++row_tile) {
        rm_hmx_run_row_tile(row_tile);
        rm_hmx_unpack_row_tile(row_tile, transform);
    }

    for (size_t block = 0; block < RM_HMX_BATCH_BLOCKS; ++block) {
        transform[block][0] -= 64 * MULTIPLICITY;
        HVX_Vector row0 = *(const HVX_Vector *)&transform[block][0];
        HVX_Vector row1 = *(const HVX_Vector *)&transform[block][64];
        message_array[first_block + block] = rm_select_peak_from_rows(row0, row1);
    }
    return 0;
}
#endif

/* Public RM codec API. */

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
 * The experimental lab can decode full 32-block batches through HMX and keeps
 * the remaining blocks on the fused HVX path.
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
    size_t i = 0;
#if HQC_RM_HMX_BATCH
    for (; i + RM_HMX_BATCH_BLOCKS <= VEC_N1_SIZE_BYTES; i += RM_HMX_BATCH_BLOCKS) {
        if (rm_decode_32_blocks_hmx(message_array, codeArray, i) < 0) {
            break;
        }
    }
#endif
    for (; i + 1 < VEC_N1_SIZE_BYTES; i += 2) {
        rm_decode_two_blocks_hvx(message_array, codeArray, i);
    }
    for (; i < VEC_N1_SIZE_BYTES; i++) {
        message_array[i] = rm_decode_one_block_hvx(&codeArray[i * MULTIPLICITY]);
    }
}
