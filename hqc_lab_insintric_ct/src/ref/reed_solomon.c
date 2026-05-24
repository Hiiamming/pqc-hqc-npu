/**
 * @file reed_solomon.c
 * @brief HQC-128 Reed-Solomon codec for the Hexagon CT intrinsic path.
 *
 * This build keeps fixed-flow ELP and error-value logic, uses fixed-flow GF
 * arithmetic, and uses HVX only for public-length syndrome/root evaluation.
 * Branchy benchmark paths, GF table multiplication, FFT root backends, and
 * portable non-Hexagon backends are intentionally absent.
 */

#include "reed_solomon.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "gf.h"
#include "parameters.h"

#ifndef __hexagon__
#error "hqc_lab_insintric CT backup is Hexagon-only; use hqc_lab_scalar for host/scalar builds"
#endif

#include <hexagon_protos.h>
#include <hexagon_types.h>

#ifdef VERBOSE
#include <stdbool.h>
#include <stdio.h>
#endif

#if defined(__GNUC__)
#define HQC_MAYBE_UNUSED __attribute__((unused))
#else
#define HQC_MAYBE_UNUSED
#endif

#define RS_SUPPORT_VEC_COUNT CEIL_DIVIDE(PARAM_N1, 64)
#if RS_SUPPORT_VEC_COUNT > 2
#error "The CT HVX Chien/Forney path assumes PARAM_N1 <= 128"
#endif
#if (2 * PARAM_DELTA) > 64
#error "The CT HVX syndrome path assumes 2*PARAM_DELTA <= 64"
#endif
#define RS_SUPPORT_LANES (RS_SUPPORT_VEC_COUNT * 64)

static uint16_t mod(uint16_t i, uint16_t modulus);
static uint16_t ct_is_zero_u16(uint16_t x);
static uint16_t ct_eq_mask_u16(uint16_t a, uint16_t b);
static uint16_t ct_lt_mask_u16(uint16_t a, uint16_t b);
static void compute_syndromes(uint16_t *syndromes, uint8_t *cdw);
static void compute_syndromes_hvx(uint16_t *syndromes, uint8_t *cdw);
static void init_elp_update_masks(void);
static uint16_t compute_elp(uint16_t *sigma, const uint16_t *syndromes);
static void compute_roots(uint8_t *error, uint16_t *sigma, uint16_t degree);
static void compute_roots_hvx(uint8_t *error, const uint16_t *sigma, uint16_t degree);
static void compute_z_poly(uint16_t *z, const uint16_t *sigma, const uint16_t degree, const uint16_t *syndromes);
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error, const uint16_t *sigma, uint16_t degree);
static void correct_errors(uint8_t *cdw, const uint16_t *error_values);
static inline HVX_Vector gf_mul_vec_by_vec_hvx(HVX_Vector a, HVX_Vector b);
static inline HVX_Vector gf_square_vec_hvx(HVX_Vector a);
static inline HVX_Vector gf_inverse_vec_hvx(HVX_Vector a);
static inline HVX_Vector hvx_reduce_xor_h(HVX_Vector v);
static inline uint16_t hvx_lane0_u16(HVX_Vector v);

static inline uint16_t rs_gf_xtime(uint16_t x) {
    uint16_t carry = x >> 7;
    return (uint16_t)(((x << 1) ^ (0x1d & (uint16_t)-carry)) & 0xff);
}

static inline uint16_t rs_gf_mul_ct(uint16_t a, uint16_t b) {
    uint16_t acc = 0;
    a &= 0xff;
    b &= 0xff;

#define RS_GF_MUL_CT_STEP(bit)                  \
    do {                                        \
        uint16_t bit_mask = (uint16_t)-((b >> (bit)) & 1); \
        acc = rs_gf_xtime(acc);                 \
        acc ^= a & bit_mask;                    \
    } while (0)

    RS_GF_MUL_CT_STEP(7);
    RS_GF_MUL_CT_STEP(6);
    RS_GF_MUL_CT_STEP(5);
    RS_GF_MUL_CT_STEP(4);
    RS_GF_MUL_CT_STEP(3);
    RS_GF_MUL_CT_STEP(2);
    RS_GF_MUL_CT_STEP(1);
    RS_GF_MUL_CT_STEP(0);

#undef RS_GF_MUL_CT_STEP

    return acc;
}

static inline uint16_t rs_gf_square_ct(uint16_t a) {
    uint32_t b = a;
    uint32_t s = b & 1;
    for (size_t i = 1; i < PARAM_M; ++i) {
        b <<= 1;
        s ^= b & (1 << (2 * i));
    }

    for (int i = 0; i < 2; ++i) {
        uint16_t mod = (uint16_t)(s >> PARAM_M);
        s &= (1 << PARAM_M) - 1;
        s ^= mod;
        s ^= (uint16_t)(mod << 2);
        s ^= (uint16_t)(mod << 3);
        s ^= (uint16_t)(mod << 4);
    }

    return (uint16_t)s;
}

static inline uint16_t rs_gf_inverse_ct(uint16_t a) {
    uint16_t inv = rs_gf_square_ct(a);
    uint16_t tmp1 = rs_gf_mul_ct(inv, a);
    inv = rs_gf_square_ct(inv);
    uint16_t tmp2 = rs_gf_mul_ct(inv, tmp1);
    tmp1 = rs_gf_mul_ct(inv, tmp2);
    inv = rs_gf_mul_ct(tmp1, inv);
    inv = rs_gf_square_ct(inv);
    inv = rs_gf_square_ct(inv);
    inv = rs_gf_square_ct(inv);
    inv = rs_gf_mul_ct(inv, tmp2);
    inv = rs_gf_square_ct(inv);
    return inv;
}

#define gf_mul rs_gf_mul_ct
#define gf_square rs_gf_square_ct
#define gf_inverse rs_gf_inverse_ct

/**
 * Returns i modulo the given modulus.
 * i must be less than 2*modulus.
 * Therefore, the return value is either i or i-modulus.
 * @returns i mod (modulus)
 * @param[in] i The integer whose modulo is taken
 * @param[in] modulus The modulus
 */
static uint16_t mod(uint16_t i, uint16_t modulus) {
    uint16_t tmp = i - modulus;

    // mask = 0xffff if(i < PARAM_GF_MUL_ORDER)
    int16_t mask = -(tmp >> 15);

    return tmp + (mask & modulus);
}

static uint16_t ct_is_zero_u16(uint16_t x) {
    return (uint16_t)(1 ^ (((uint16_t)(x | (uint16_t)-x)) >> 15));
}

static uint16_t HQC_MAYBE_UNUSED ct_eq_mask_u16(uint16_t a, uint16_t b) {
    return (uint16_t)-ct_is_zero_u16((uint16_t)(a ^ b));
}

static uint16_t HQC_MAYBE_UNUSED ct_lt_mask_u16(uint16_t a, uint16_t b) {
    return (uint16_t)-((uint16_t)(a - b) >> 15);
}

static uint16_t elp_update_masks[2 * PARAM_DELTA][64] __attribute__((aligned(128)));
static int elp_update_masks_ready = 0;

static void init_elp_update_masks(void) {
    if (elp_update_masks_ready) {
        return;
    }

    for (uint16_t mu = 0; mu < (2 * PARAM_DELTA); ++mu) {
        for (uint16_t i = 1; i <= PARAM_DELTA; ++i) {
            elp_update_masks[mu][i] = (uint16_t)-(uint16_t)(i <= (mu + 1));
        }
    }

    elp_update_masks_ready = 1;
}

/**
 * @brief Computes the generator polynomial of the primitive Reed-Solomon code with given parameters.
 *
 * Code length is 2^m-1. <br>
 * PARAM_DELTA is the targeted correction capacity of the code
 * and receives the real correction capacity (which is at least equal to the target). <br>
 * gf_exp and gf_log are arrays giving antilog and log of GF(2^m) elements.
 *
 * @param[out] poly Array of size (2*PARAM_DELTA + 1) receiving the coefficients of the generator polynomial
 */
void compute_generator_poly(uint16_t *poly) {
    poly[0] = 1;
    int tmp_degree = 0;

    for (uint16_t i = 1; i < (2 * PARAM_DELTA + 1); ++i) {
        for (size_t j = tmp_degree; j; --j) {
            poly[j] = gf_exp[mod(gf_log[poly[j]] + i, PARAM_GF_MUL_ORDER)] ^ poly[j - 1];
        }

        poly[0] = gf_exp[mod(gf_log[poly[0]] + i, PARAM_GF_MUL_ORDER)];
        poly[++tmp_degree] = 1;
    }

    printf("\n");
    for (int i = 0; i < (PARAM_G); ++i) {
        printf("%d, ", poly[i]);
    }
    printf("\n");
}

/**
 * @brief Encodes a message message of PARAM_K bits to a Reed-Solomon codeword codeword of PARAM_N1 bytes
 *
 * Following @cite lin1983error (Chapter 4 - Cyclic Codes),
 * We perform a systematic encoding using a linear (PARAM_N1 - PARAM_K)-stage shift register
 * with feedback connections based on the generator polynomial PARAM_RS_POLY of the Reed-Solomon code.
 *
 * @param[out] cdw Array of size VEC_N1_SIZE_64 receiving the encoded message
 * @param[in] msg Array of size VEC_K_SIZE_64 storing the message
 */
void reed_solomon_encode(uint64_t *cdw, const uint64_t *msg) {
    size_t i, j, k;
    uint8_t gate_value = 0;

    uint16_t tmp[PARAM_G] = {0};
    uint16_t PARAM_RS_POLY[] = {RS_POLY_COEFS};

    uint8_t msg_bytes[PARAM_K] = {0};
    uint8_t cdw_bytes[PARAM_N1] = {0};

    memcpy(msg_bytes, msg, PARAM_K);

    for (i = 0; i < PARAM_K; ++i) {
        gate_value = msg_bytes[PARAM_K - 1 - i] ^ cdw_bytes[PARAM_N1 - PARAM_K - 1];

        for (j = 0; j < PARAM_G; ++j) {
            tmp[j] = gf_mul(gate_value, PARAM_RS_POLY[j]);
        }

        for (k = PARAM_N1 - PARAM_K - 1; k; --k) {
            cdw_bytes[k] = cdw_bytes[k - 1] ^ tmp[k];
        }

        cdw_bytes[0] = tmp[0];
    }

    memcpy(cdw_bytes + PARAM_N1 - PARAM_K, msg_bytes, PARAM_K);
    memcpy(cdw, cdw_bytes, PARAM_N1);
}

/**
 * @brief Computes 2 * PARAM_DELTA syndromes
 *
 * The intrinsic Hexagon build uses compute_syndromes_hvx by default. It maps
 * the 30 syndrome lanes into one HVX vector and multiplies each received byte
 * by the pre-transposed alpha powers with a fixed-flow vector GF multiplier.
 *
 * @param[out] syndromes Array of size 2 * PARAM_DELTA receiving the computed syndromes
 * @param[in] cdw Array of size PARAM_N1 storing the received vector
 */
void compute_syndromes(uint16_t *syndromes, uint8_t *cdw) {
    compute_syndromes_hvx(syndromes, cdw);
}

static uint16_t alpha_ji_pow[PARAM_N1 - 1][64] __attribute__((aligned(128)));
static int alpha_ji_pow_ready = 0;

static void init_alpha_ji_pow(void) {
    if (alpha_ji_pow_ready) {
        return;
    }

    for (size_t j = 0; j < PARAM_N1 - 1; ++j) {
        for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
            alpha_ji_pow[j][i] = alpha_ij_pow[i][j];
        }
    }

    alpha_ji_pow_ready = 1;
}

static inline HVX_Vector gf_xtime_hvx(HVX_Vector x) {
    HVX_Vector zero = Q6_V_vzero();
    HVX_Vector one = Q6_Vh_vsplat_R(1);
    HVX_Vector taps = Q6_Vh_vsplat_R(0x1d);
    HVX_Vector byte_mask = Q6_Vh_vsplat_R(0xff);
    HVX_Vector carry = Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(x, 7), one);
    HVX_Vector carry_mask = Q6_Vh_vsub_VhVh(zero, carry);
    HVX_Vector reduced = Q6_V_vxor_VV(Q6_Vh_vasl_VhR(x, 1), Q6_V_vand_VV(carry_mask, taps));
    return Q6_V_vand_VV(reduced, byte_mask);
}

static HVX_Vector gf_mul_scalar_by_vec_hvx(uint16_t a, HVX_Vector b) {
    HVX_Vector acc = Q6_V_vzero();
    HVX_Vector avec = Q6_Vh_vsplat_R((int)a);
    HVX_Vector one = Q6_Vh_vsplat_R(1);
    HVX_Vector zero = Q6_V_vzero();

#define GF_MUL_VEC_STEP(bit)                                      \
    do {                                                          \
        HVX_Vector bbit = Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(b, (bit)), one); \
        HVX_Vector bit_mask = Q6_Vh_vsub_VhVh(zero, bbit);        \
        acc = gf_xtime_hvx(acc);                                  \
        acc = Q6_V_vxor_VV(acc, Q6_V_vand_VV(avec, bit_mask));    \
    } while (0)

    GF_MUL_VEC_STEP(7);
    GF_MUL_VEC_STEP(6);
    GF_MUL_VEC_STEP(5);
    GF_MUL_VEC_STEP(4);
    GF_MUL_VEC_STEP(3);
    GF_MUL_VEC_STEP(2);
    GF_MUL_VEC_STEP(1);
    GF_MUL_VEC_STEP(0);

#undef GF_MUL_VEC_STEP

    return acc;
}

static inline HVX_Vector gf_mul_vec_by_vec_hvx(HVX_Vector a, HVX_Vector b) {
    HVX_Vector acc = Q6_V_vzero();
    HVX_Vector one = Q6_Vh_vsplat_R(1);
    HVX_Vector zero = Q6_V_vzero();

#define GF_MUL_VV_STEP(bit)                                      \
    do {                                                         \
        HVX_Vector bbit = Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(b, (bit)), one); \
        HVX_Vector bit_mask = Q6_Vh_vsub_VhVh(zero, bbit);       \
        acc = gf_xtime_hvx(acc);                                 \
        acc = Q6_V_vxor_VV(acc, Q6_V_vand_VV(a, bit_mask));      \
    } while (0)

    GF_MUL_VV_STEP(7);
    GF_MUL_VV_STEP(6);
    GF_MUL_VV_STEP(5);
    GF_MUL_VV_STEP(4);
    GF_MUL_VV_STEP(3);
    GF_MUL_VV_STEP(2);
    GF_MUL_VV_STEP(1);
    GF_MUL_VV_STEP(0);

#undef GF_MUL_VV_STEP

    return acc;
}

static inline HVX_Vector gf_square_vec_hvx(HVX_Vector a) {
    return gf_mul_vec_by_vec_hvx(a, a);
}

static inline HVX_Vector gf_inverse_vec_hvx(HVX_Vector a) {
    HVX_Vector inv = gf_square_vec_hvx(a);
    HVX_Vector tmp1 = gf_mul_vec_by_vec_hvx(inv, a);
    inv = gf_square_vec_hvx(inv);
    HVX_Vector tmp2 = gf_mul_vec_by_vec_hvx(inv, tmp1);
    tmp1 = gf_mul_vec_by_vec_hvx(inv, tmp2);
    inv = gf_mul_vec_by_vec_hvx(tmp1, inv);
    inv = gf_square_vec_hvx(inv);
    inv = gf_square_vec_hvx(inv);
    inv = gf_square_vec_hvx(inv);
    inv = gf_mul_vec_by_vec_hvx(inv, tmp2);
    inv = gf_square_vec_hvx(inv);
    return inv;
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

static inline uint16_t hvx_lane0_u16(HVX_Vector v) {
    uint16_t lane[64] __attribute__((aligned(128)));
    *(HVX_Vector *)lane = v;
    return lane[0];
}

static void compute_syndromes_hvx(uint16_t *syndromes, uint8_t *cdw) {
    uint16_t out[64] __attribute__((aligned(128)));
    HVX_Vector acc = Q6_V_vzero();

    init_alpha_ji_pow();

#pragma unroll
    for (size_t j = 1; j < PARAM_N1; ++j) {
        HVX_Vector coeffs = *(const HVX_Vector *)&alpha_ji_pow[j - 1][0];
        HVX_Vector prod = gf_mul_scalar_by_vec_hvx(cdw[j], coeffs);
        acc = Q6_V_vxor_VV(acc, prod);
    }

    acc = Q6_V_vxor_VV(acc, Q6_Vh_vsplat_R(cdw[0]));
    *(HVX_Vector *)&out[0] = acc;
    memcpy(syndromes, out, 2 * PARAM_DELTA * sizeof(uint16_t));
}

/**
 * @brief Computes the error locator polynomial (ELP) sigma
 *
 * This is a masked fixed-flow Berlekamp-Massey implementation (see
 * @cite lin1983error, Chapter 6, BCH codes).
 *
 * In the default masked path, we use the letter p for rho, initialized at -1.
 * The array X_sigma_p represents the
 * polynomial X^(mu-rho)*sigma_p(X). <br> Instead of maintaining a list of sigmas, we update in place both sigma and
 * X_sigma_p. <br> sigma_copy serves as a temporary save of sigma in case X_sigma_p needs to be updated. <br> We can
 * properly correct only if the degree of sigma does not exceed PARAM_DELTA. This means only the first PARAM_DELTA + 1
 * coefficients of sigma are of value and we only need to save its first PARAM_DELTA - 1 coefficients.
 *
 * @returns the degree of the ELP sigma
 * @param[out] sigma Array of size (at least) PARAM_DELTA receiving the ELP
 * @param[in] syndromes Array of size (at least) 2*PARAM_DELTA storing the syndromes
 */
static uint16_t compute_elp(uint16_t *sigma, const uint16_t *syndromes) {
    uint16_t sigma_local[64] __attribute__((aligned(128))) = {0};
    uint16_t sigma_copy[64] __attribute__((aligned(128))) = {0};
    uint16_t X_sigma_p[64] __attribute__((aligned(128))) = {0};
    uint16_t X_sigma_p_next[64] __attribute__((aligned(128))) = {0};
    uint16_t syndrome_terms[64] __attribute__((aligned(128))) = {0};
    uint16_t deg_sigma = 0;
    uint16_t deg_sigma_p = 0;
    uint16_t deg_sigma_copy = 0;
    uint16_t pp = (uint16_t)-1;  // 2*rho
    uint16_t d_p = 1;
    uint16_t d = syndromes[0];

    uint16_t mask1, mask2, mask12;
    uint16_t deg_X, deg_X_sigma_p;
    uint16_t dd;
    uint16_t mu;

    uint16_t i;

    init_elp_update_masks();

    sigma_local[0] = 1;
    X_sigma_p[1] = 1;
    for (mu = 0; (mu < (2 * PARAM_DELTA)); ++mu) {
        // Save sigma in case we need it to update X_sigma_p
        *(HVX_Vector *)&sigma_copy[0] = *(const HVX_Vector *)&sigma_local[0];
        deg_sigma_copy = deg_sigma;

        dd = gf_mul(d, gf_inverse(d_p));

        HVX_Vector sigma_vec = *(const HVX_Vector *)&sigma_local[0];
        HVX_Vector update_vec = gf_mul_scalar_by_vec_hvx(dd, *(const HVX_Vector *)&X_sigma_p[0]);
        update_vec = Q6_V_vand_VV(update_vec, *(const HVX_Vector *)&elp_update_masks[mu][0]);
        sigma_vec = Q6_V_vxor_VV(sigma_vec, update_vec);
        *(HVX_Vector *)&sigma_local[0] = sigma_vec;

        deg_X = mu - pp;
        deg_X_sigma_p = deg_X + deg_sigma_p;

        // mask1 = 0xffff if(d != 0) and 0 otherwise
        mask1 = -((uint16_t)-d >> 15);

        // mask2 = 0xffff if(deg_X_sigma_p > deg_sigma) and 0 otherwise
        mask2 = -((uint16_t)(deg_sigma - deg_X_sigma_p) >> 15);

        // mask12 = 0xffff if the deg_sigma increased and 0 otherwise
        volatile uint16_t mask12__ = mask1 & mask2;
        mask12 = mask12__;
        deg_sigma ^= mask12 & (deg_X_sigma_p ^ deg_sigma);

        if (mu == (2 * PARAM_DELTA - 1)) {
            break;
        }

        pp ^= mask12 & (mu ^ pp);
        d_p ^= mask12 & (d ^ d_p);
        X_sigma_p_next[0] = 0;
        for (i = 1; i <= PARAM_DELTA; ++i) {
            X_sigma_p_next[i] = (mask12 & sigma_copy[i - 1]) ^ (~mask12 & X_sigma_p[i - 1]);
        }
        *(HVX_Vector *)&X_sigma_p[0] = *(const HVX_Vector *)&X_sigma_p_next[0];

        deg_sigma_p ^= mask12 & (deg_sigma_copy ^ deg_sigma_p);
        d = syndromes[mu + 1];

        for (i = 1; i <= PARAM_DELTA; ++i) {
            syndrome_terms[i] = (i <= (mu + 1)) ? syndromes[mu + 1 - i] : 0;
        }
        d ^= hvx_lane0_u16(hvx_reduce_xor_h(
            gf_mul_vec_by_vec_hvx(*(const HVX_Vector *)&sigma_local[0],
                                  *(const HVX_Vector *)&syndrome_terms[0])));
    }

    memcpy(sigma, sigma_local, (PARAM_DELTA + 1) * sizeof(uint16_t));
    return deg_sigma;
}

/**
 * @brief Computes the error polynomial error from the error locator polynomial sigma
 *
 * The backend is a fixed-flow HVX Chien search over the public shortened
 * RS support. It evaluates all PARAM_DELTA + 1 coefficients with no
 * secret-dependent branch or table index.
 *
 * @param[out] error Array of 2^PARAM_M elements receiving the error polynomial
 * @param[in] sigma Array of 2^PARAM_FFT elements storing the error locator polynomial
 */
static void compute_roots(uint8_t *error, uint16_t *sigma, uint16_t degree) {
    compute_roots_hvx(error, sigma, degree);
}

static uint16_t rs_support_powers[PARAM_DELTA + 1][RS_SUPPORT_LANES] __attribute__((aligned(128)));
static uint16_t rs_support_beta[RS_SUPPORT_LANES] __attribute__((aligned(128)));
static int rs_support_powers_ready = 0;

/**
 * @brief Precompute x_i^j for the shortened RS support used by HVX Chien.
 *
 * Lane i corresponds to x_i = alpha^{-i} = gf_exp[255 - i] for i < PARAM_N1.
 * Lanes [PARAM_N1, RS_SUPPORT_LANES) are padding so the table can be loaded as
 * one or more 128-byte HVX vectors. Those lanes are ignored when writing the
 * error vector.
 */
static void init_rs_support_powers(void) {
    if (rs_support_powers_ready) {
        return;
    }

    for (size_t lane = 0; lane < RS_SUPPORT_LANES; ++lane) {
        uint16_t x = (lane < PARAM_N1) ? gf_exp[PARAM_GF_MUL_ORDER - lane] : 0;
        rs_support_beta[lane] = (lane < PARAM_N1) ? gf_exp[lane] : 0;
        rs_support_powers[0][lane] = 1;
        for (size_t j = 1; j <= PARAM_DELTA; ++j) {
            rs_support_powers[j][lane] = gf_mul(rs_support_powers[j - 1][lane], x);
        }
    }

    rs_support_powers_ready = 1;
}

/**
 * @brief HVX Chien search over the shortened RS support.
 *
 * This evaluates sigma(x_i) = sum_j sigma_j x_i^j for all 46 support points
 * in parallel. It deliberately uses the fixed-flow vector GF multiplier rather
 * than the scalar GF table, because the win comes from evaluating all support
 * points at once.
 */
static void compute_roots_hvx(uint8_t *error, const uint16_t *sigma, uint16_t degree) {
    uint16_t eval[RS_SUPPORT_LANES] __attribute__((aligned(128)));
    HVX_Vector acc[RS_SUPPORT_VEC_COUNT];

    init_rs_support_powers();
    memset(error, 0, PARAM_N1);

    (void)degree;
    for (size_t v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
        acc[v] = Q6_V_vzero();
    }

#pragma unroll
    for (size_t j = 0; j <= PARAM_DELTA; ++j) {
        for (size_t v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
            HVX_Vector powers = *(const HVX_Vector *)&rs_support_powers[j][v * 64];
            acc[v] = Q6_V_vxor_VV(acc[v], gf_mul_scalar_by_vec_hvx(sigma[j], powers));
        }
    }

    for (size_t v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
        *(HVX_Vector *)&eval[v * 64] = acc[v];
    }
    for (size_t i = 0; i < PARAM_N1; ++i) {
        error[i] = (uint8_t)ct_is_zero_u16(eval[i]);
    }
}

/**
 * @brief Computes the polynomial z(x)
 *
 * This path keeps masked PARAM_DELTA loops.
 *
 * See @cite lin1983error (Chapter 6 - BCH Codes) for more details.
 *
 * @param[out] z Array of PARAM_DELTA + 1 elements receiving the polynomial z(x)
 * @param[in] sigma Array of 2^PARAM_FFT elements storing the error locator polynomial
 * @param[in] degree Integer that is the degree of polynomial sigma
 * @param[in] syndromes Array of 2 * PARAM_DELTA storing the syndromes
 */
static void compute_z_poly(uint16_t *z, const uint16_t *sigma, const uint16_t degree, const uint16_t *syndromes) {
    size_t i, j;
    uint16_t mask;

    z[0] = 1;

    for (i = 1; i < PARAM_DELTA + 1; ++i) {
        mask = -((uint16_t)(i - degree - 1) >> 15);
        z[i] = mask & sigma[i];
    }

    z[1] ^= syndromes[0];

    for (i = 2; i <= PARAM_DELTA; ++i) {
        mask = -((uint16_t)(i - degree - 1) >> 15);
        z[i] ^= mask & syndromes[i - 1];

        for (j = 1; j < i; ++j) {
            z[i] ^= mask & gf_mul(sigma[j], syndromes[i - j - 1]);
        }
    }
}

/**
 * @brief Computes the error values
 *
 * This path keeps fixed-loop masked placement of located errors and evaluates
 * the formal derivative sigma'(beta_i^{-1}) as the Forney denominator.
 *
 * See @cite lin1983error (Chapter 6 - BCH Codes) for more details.
 *
 * @param[out] error_values Array of PARAM_DELTA elements receiving the error values
 * @param[in] z Array of PARAM_DELTA + 1 elements storing the polynomial z(x)
 * @param[in] error Array storing the error
 */
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error, const uint16_t *sigma, uint16_t degree) {
    (void)degree;
    uint16_t values[RS_SUPPORT_LANES] __attribute__((aligned(128)));
    uint16_t error_masks[RS_SUPPORT_LANES] __attribute__((aligned(128)));

    init_rs_support_powers();

    for (size_t i = 0; i < RS_SUPPORT_LANES; ++i) {
        uint16_t error_bit = (i < PARAM_N1) ? (uint16_t)(error[i] & 1u) : 0;
        uint16_t active = (uint16_t)-error_bit;
        error_masks[i] = active;
    }

    for (size_t v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
        size_t base = v * 64;
        HVX_Vector numerator = Q6_Vh_vsplat_R(1);
        HVX_Vector denominator = Q6_V_vzero();
        HVX_Vector beta;
        HVX_Vector denominator_inverse;
        HVX_Vector out;

#pragma unroll
        for (size_t j = 1; j <= PARAM_DELTA; ++j) {
            HVX_Vector powers = *(const HVX_Vector *)&rs_support_powers[j][base];
            numerator = Q6_V_vxor_VV(numerator, gf_mul_scalar_by_vec_hvx(z[j], powers));
        }

#pragma unroll
        for (size_t j = 1; j <= PARAM_DELTA; j += 2) {
            HVX_Vector powers = *(const HVX_Vector *)&rs_support_powers[j - 1][base];
            denominator = Q6_V_vxor_VV(denominator, gf_mul_scalar_by_vec_hvx(sigma[j], powers));
        }

        beta = *(const HVX_Vector *)&rs_support_beta[base];
        denominator_inverse = gf_inverse_vec_hvx(denominator);
        out = gf_mul_vec_by_vec_hvx(numerator, beta);
        out = gf_mul_vec_by_vec_hvx(out, denominator_inverse);
        out = Q6_V_vand_VV(out, *(const HVX_Vector *)&error_masks[base]);
        *(HVX_Vector *)&values[base] = out;
    }

    for (size_t i = 0; i < PARAM_N1; ++i) {
        error_values[i] = values[i];
    }
}

/**
 * @brief Correct the errors
 *
 * @param[out] cdw Array of PARAM_N1 elements receiving the corrected vector
 * @param[in] error_values Array of PARAM_DELTA elements storing the error values
 */
static void correct_errors(uint8_t *cdw, const uint16_t *error_values) {
    for (size_t i = 0; i < PARAM_N1; ++i) {
        cdw[i] ^= error_values[i];
    }
}

/**
 * @brief Decodes the received word
 *
 * This function relies on six steps:
 * -# Compute the 2·PARAM_DELTA syndromes.
 * -# Compute the error-locator polynomial σ(x).
 * -# Find the roots of σ(x) with fixed-flow HVX shortened-support Chien search.
 * -# Compute the error-evaluator polynomial z(x).
 * -# Compute the error values at each located position.
 * -# Correct the received polynomial by subtracting the error values.
 *
 * For a more complete picture on Reed-Solomon decoding, see Shu. Lin and Daniel J. Costello in Error Control Coding:
 * Fundamentals and Applications @cite lin1983error
 *
 * @param[out] msg Array of size VEC_K_SIZE_64 receiving the decoded message
 * @param[in] cdw Array of size VEC_N1_SIZE_64 storing the received word
 */
void reed_solomon_decode(uint64_t *msg, uint64_t *cdw) {
    uint8_t cdw_bytes[PARAM_N1] = {0};
    uint16_t syndromes[2 * PARAM_DELTA] = {0};
    uint16_t sigma[1 << PARAM_FFT] = {0};
    uint8_t error[PARAM_N1] = {0};
    uint16_t z[PARAM_N1] = {0};
    uint16_t error_values[PARAM_N1] = {0};
    uint16_t deg;

    // Copy the vector in an array of bytes
    memcpy(cdw_bytes, cdw, PARAM_N1);

    // Calculate the 2*PARAM_DELTA syndromes
    compute_syndromes(syndromes, cdw_bytes);

    // Compute the error locator polynomial sigma
    // Sigma's degree is at most PARAM_DELTA.
    deg = compute_elp(sigma, syndromes);

    // Compute the error polynomial error
    compute_roots(error, sigma, deg);

    // Compute the polynomial z(x)
    compute_z_poly(z, sigma, deg, syndromes);

    // Compute the error values
    compute_error_values(error_values, z, error, sigma, deg);

    // Correct the errors
    correct_errors(cdw_bytes, error_values);

    // Retrieve the message from the decoded codeword
    memcpy(msg, cdw_bytes + (PARAM_G - 1), PARAM_K);

#ifdef VERBOSE
    printf("\n\nThe syndromes: ");
    for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
        printf("%u ", syndromes[i]);
    }
    printf("\n\nThe error locator polynomial: sigma(x) = ");
    bool first_coeff = true;
    if (sigma[0]) {
        printf("%u", sigma[0]);
        first_coeff = false;
    }
    for (size_t i = 1; i < (1 << PARAM_FFT); ++i) {
        if (sigma[i] == 0)
            continue;
        if (!first_coeff)
            printf(" + ");
        first_coeff = false;
        if (sigma[i] != 1)
            printf("%u ", sigma[i]);
        if (i == 1)
            printf("x");
        else
            printf("x^%zu", i);
    }
    if (first_coeff)
        printf("0");

    printf("\n\nThe polynomial: z(x) = ");
    bool first_coeff_1 = true;
    if (z[0]) {
        printf("%u", z[0]);
        first_coeff_1 = false;
    }
    for (size_t i = 1; i < (PARAM_DELTA + 1); ++i) {
        if (z[i] == 0)
            continue;
        if (!first_coeff_1)
            printf(" + ");
        first_coeff_1 = false;
        if (z[i] != 1)
            printf("%u ", z[i]);
        if (i == 1)
            printf("x");
        else
            printf("x^%zu", i);
    }
    if (first_coeff_1)
        printf("0");

    printf("\n\nThe pairs of (error locator numbers, error values): ");
    size_t j = 0;
    for (size_t i = 0; i < PARAM_N1; ++i) {
        if (error[i]) {
            printf("(%zu, %d) ", i, error_values[j]);
            j++;
        }
    }
    printf("\n");
#endif

    memset(cdw_bytes, 0, sizeof cdw_bytes);
}

#if defined(HQC_ENABLE_SUBSTAGE_BENCH)
void hqc_rs_bench_compute_syndromes(uint16_t *syndromes, uint8_t *cdw) {
    compute_syndromes(syndromes, cdw);
}

uint16_t hqc_rs_bench_compute_elp(uint16_t *sigma, const uint16_t *syndromes) {
    return compute_elp(sigma, syndromes);
}

void hqc_rs_bench_compute_roots(uint8_t *error, uint16_t *sigma, uint16_t degree) {
    compute_roots(error, sigma, degree);
}

void hqc_rs_bench_compute_z_poly(uint16_t *z, const uint16_t *sigma, uint16_t degree, const uint16_t *syndromes) {
    compute_z_poly(z, sigma, degree, syndromes);
}

void hqc_rs_bench_compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error, const uint16_t *sigma, uint16_t degree) {
    compute_error_values(error_values, z, error, sigma, degree);
}

void hqc_rs_bench_correct_errors(uint8_t *cdw, const uint16_t *error_values) {
    correct_errors(cdw, error_values);
}
#endif
