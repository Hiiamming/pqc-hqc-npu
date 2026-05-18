/**
 * @file reed_solomon.c
 * @brief HQC-128 Reed-Solomon codec with scalar, HVX, and benchmark fast paths.
 *
 * The default intrinsic build keeps fixed-flow ELP and error-value logic, while
 * using Hexagon HVX for syndrome computation and shortened-support Chien root
 * search. Benchmark-only flags enable branchy RS algebra, GF lookup tables,
 * and HVX root evaluation when side-channel behavior is outside the current
 * benchmark scope.
 */

#include "reed_solomon.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "fft.h"
#include "gf.h"
#include "parameters.h"
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && (defined(HQC_USE_HVX_RS_SYNDROME) || defined(HQC_RS_ROOTS_HVX))
#include <hexagon_protos.h>
#include <hexagon_types.h>
#endif
#ifdef VERBOSE
#include <stdbool.h>
#include <stdio.h>
#endif

/* The HVX Chien-search path now scales to PARAM_N1 up to 128 by splitting
 * the support across multiple 64-lane halfword vectors. HQC-128 (N1=46) and
 * HQC-192 (N1=56) use 1 vector; HQC-256 (N1=90) uses 2 vectors. The exact
 * vector count is computed at compile time as RS_SUPPORT_VEC_COUNT inside
 * the gated section below; that section refuses to compile if any future
 * parameter set ever pushed PARAM_N1 beyond 128. */

static uint16_t mod(uint16_t i, uint16_t modulus);
#if !defined(HQC_RS_ROOTS_FFT)
static uint16_t ct_is_zero_u16(uint16_t x);
#endif
static void compute_syndromes(uint16_t *syndromes, uint8_t *cdw);
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_USE_HVX_RS_SYNDROME)
static void compute_syndromes_hvx(uint16_t *syndromes, uint8_t *cdw);
#endif
static uint16_t compute_elp(uint16_t *sigma, const uint16_t *syndromes);
static void compute_roots(uint8_t *error, uint16_t *sigma, uint16_t degree);
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_RS_ROOTS_HVX) && !defined(HQC_RS_ROOTS_FFT)
static void compute_roots_hvx(uint8_t *error, const uint16_t *sigma, uint16_t degree);
#endif
static void compute_z_poly(uint16_t *z, const uint16_t *sigma, const uint16_t degree, const uint16_t *syndromes);
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error, const uint16_t *sigma, uint16_t degree);
static void correct_errors(uint8_t *cdw, const uint16_t *error_values);

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

#if !defined(HQC_RS_ROOTS_FFT)
static uint16_t ct_is_zero_u16(uint16_t x) {
    return (uint16_t)(1 ^ (((uint16_t)(x | (uint16_t)-x)) >> 15));
}
#endif

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
#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_USE_HVX_RS_SYNDROME)
    compute_syndromes_hvx(syndromes, cdw);
#else
    for (size_t i = 0; i < 2 * PARAM_DELTA; ++i) {
        for (size_t j = 1; j < PARAM_N1; ++j) {
            syndromes[i] ^= gf_mul(cdw[j], alpha_ij_pow[i][j - 1]);
        }
        syndromes[i] ^= cdw[0];
    }
#endif
}

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && (defined(HQC_USE_HVX_RS_SYNDROME) || defined(HQC_RS_ROOTS_HVX))
#if defined(HQC_USE_HVX_RS_SYNDROME)
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
#endif

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

#if defined(HQC_USE_HVX_RS_SYNDROME)
static void compute_syndromes_hvx(uint16_t *syndromes, uint8_t *cdw) {
    uint16_t out[64] __attribute__((aligned(128)));
    HVX_Vector acc = Q6_V_vzero();

    init_alpha_ji_pow();

    for (size_t j = 1; j < PARAM_N1; ++j) {
        HVX_Vector coeffs = *(const HVX_Vector *)&alpha_ji_pow[j - 1][0];
        HVX_Vector prod = gf_mul_scalar_by_vec_hvx(cdw[j], coeffs);
        acc = Q6_V_vxor_VV(acc, prod);
    }

    acc = Q6_V_vxor_VV(acc, Q6_Vh_vsplat_R(cdw[0]));
    *(HVX_Vector *)&out[0] = acc;
    memcpy(syndromes, out, 2 * PARAM_DELTA * sizeof(uint16_t));
}
#endif
#endif

/**
 * @brief Computes the error locator polynomial (ELP) sigma
 *
 * The default path is a masked fixed-flow Berlekamp-Massey implementation
 * (see @cite lin1983error, Chapter 6, BCH codes). HQC_RS_FAST_NON_CT=1
 * switches to a branchy benchmark path that tracks the actual locator degree
 * and the actual auxiliary-polynomial degree. That fast path is not promoted
 * to the side-channel-oriented default because its control flow depends on
 * decoded error structure.
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
#if defined(HQC_RS_FAST_NON_CT)
    uint16_t b[PARAM_DELTA + 1] = {0};
    uint16_t t[PARAM_DELTA + 1] = {0};
    uint16_t deg_sigma = 0;
    uint16_t deg_b = 0;
    uint16_t m = 1;
    uint16_t d_p = 1;

    sigma[0] = 1;
    b[0] = 1;

    for (uint16_t mu = 0; mu < (2 * PARAM_DELTA); ++mu) {
        uint16_t d = syndromes[mu];

        for (uint16_t i = 1; i <= deg_sigma; ++i) {
            d ^= gf_mul(sigma[i], syndromes[mu - i]);
        }

        if (d != 0) {
            uint16_t dd = gf_mul(d, gf_inverse(d_p));

            memcpy(t, sigma, sizeof(t));
            uint16_t update_degree = m + deg_b;
            if (update_degree > PARAM_DELTA) {
                update_degree = PARAM_DELTA;
            }
            for (uint16_t i = m; i <= update_degree; ++i) {
                sigma[i] ^= gf_mul(dd, b[i - m]);
            }

            if ((uint16_t)(2 * deg_sigma) <= mu) {
                uint16_t old_deg_sigma = deg_sigma;
                deg_sigma = mu + 1 - deg_sigma;
                memcpy(b, t, sizeof(b));
                deg_b = old_deg_sigma;
                m = 1;
                d_p = d;
            } else {
                ++m;
            }
        } else {
            ++m;
        }
    }

    return deg_sigma;
#else
    uint16_t deg_sigma = 0;
    uint16_t deg_sigma_p = 0;
    uint16_t deg_sigma_copy = 0;
    uint16_t sigma_copy[PARAM_DELTA + 1] = {0};
    uint16_t X_sigma_p[PARAM_DELTA + 1] = {0, 1};
    uint16_t pp = (uint16_t)-1;  // 2*rho
    uint16_t d_p = 1;
    uint16_t d = syndromes[0];

    uint16_t mask1, mask2, mask12;
    uint16_t deg_X, deg_X_sigma_p;
    uint16_t dd;
    uint16_t mu;

    uint16_t i;

    sigma[0] = 1;
    for (mu = 0; (mu < (2 * PARAM_DELTA)); ++mu) {
        // Save sigma in case we need it to update X_sigma_p
        memcpy(sigma_copy, sigma, 2 * (PARAM_DELTA));
        deg_sigma_copy = deg_sigma;

        dd = gf_mul(d, gf_inverse(d_p));

        for (i = 1; (i <= mu + 1) && (i <= PARAM_DELTA); ++i) {
            sigma[i] ^= gf_mul(dd, X_sigma_p[i]);
        }

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
        for (i = PARAM_DELTA; i; --i) {
            X_sigma_p[i] = (mask12 & sigma_copy[i - 1]) ^ (~mask12 & X_sigma_p[i - 1]);
        }

        deg_sigma_p ^= mask12 & (deg_sigma_copy ^ deg_sigma_p);
        d = syndromes[mu + 1];

        for (i = 1; (i <= mu + 1) && (i <= PARAM_DELTA); ++i) {
            d ^= gf_mul(sigma[i], syndromes[mu + 1 - i]);
        }
    }

    return deg_sigma;
#endif
}

/**
 * @brief Computes the error polynomial error from the error locator polynomial sigma
 *
 * The default backend is a fixed-flow Chien search over the public shortened
 * RS support. Define HQC_RS_ROOTS_FFT=1 to use the original additive-FFT
 * root-finding backend for differential testing and reference comparison.
 * Define HQC_RS_ROOTS_HVX=1 on Hexagon to evaluate the locator across all
 * PARAM_N1 shortened support positions in one HVX vector. The HVX roots path
 * is benchmark-only and gated separately from the default scalar Chien backend.
 *
 * @param[out] error Array of 2^PARAM_M elements receiving the error polynomial
 * @param[in] sigma Array of 2^PARAM_FFT elements storing the error locator polynomial
 */
static void compute_roots(uint8_t *error, uint16_t *sigma, uint16_t degree) {
#if defined(HQC_RS_ROOTS_FFT)
    (void)degree;
    uint16_t w[1 << PARAM_M] = {0};

    fft(w, sigma, PARAM_DELTA + 1);
    fft_retrieve_error_poly(error, w);
#elif defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_RS_ROOTS_HVX)
    compute_roots_hvx(error, sigma, degree);
#else
#if !defined(HQC_RS_FAST_NON_CT)
    (void)degree;
#endif
    memset(error, 0, 1 << PARAM_M);

    for (size_t i = 0; i < PARAM_N1; ++i) {
        uint16_t x = gf_exp[PARAM_GF_MUL_ORDER - i];
#if defined(HQC_RS_FAST_NON_CT)
        uint16_t acc = sigma[degree];

        for (size_t j = degree; j; --j) {
            acc = gf_mul(acc, x) ^ sigma[j - 1];
        }
#else
        uint16_t acc = sigma[PARAM_DELTA];

        for (size_t j = PARAM_DELTA; j; --j) {
            acc = gf_mul(acc, x) ^ sigma[j - 1];
        }
#endif

        error[i] = (uint8_t)ct_is_zero_u16(acc);
    }
#endif
}

#if defined(__hexagon__) && defined(HQC_USE_HVX_INTRINSICS) && defined(HQC_RS_ROOTS_HVX) && !defined(HQC_RS_ROOTS_FFT)
/* Lane-block count needed to cover all PARAM_N1 support positions with one
 * or more 64-lane HVX halfword vectors.
 *
 *   HQC-128 : PARAM_N1 = 46  -> 1 vector  (18 padding lanes)
 *   HQC-192 : PARAM_N1 = 56  -> 1 vector  ( 8 padding lanes)
 *   HQC-256 : PARAM_N1 = 90  -> 2 vectors (38 padding lanes in the high vec)
 *
 * Padding lanes hold the value 0 in `rs_support_powers`, so they contribute
 * `sigma[j] * 0 = 0` to the accumulator and never disturb the real lanes.
 * Capped at 2 because no real HQC parameter set has PARAM_N1 > 128. */
#define RS_SUPPORT_VEC_COUNT CEIL_DIVIDE(PARAM_N1, 64)
#if RS_SUPPORT_VEC_COUNT > 2
#error "HQC_RS_ROOTS_HVX assumes PARAM_N1 <= 128 (2 HVX halfword vectors)."
#endif

static uint16_t rs_support_powers[PARAM_DELTA + 1][RS_SUPPORT_VEC_COUNT * 64]
    __attribute__((aligned(128)));
static int rs_support_powers_ready = 0;

/**
 * @brief Precompute x_i^j for the shortened RS support used by HVX Chien.
 *
 * Lane i corresponds to x_i = alpha^{-i} = gf_exp[255 - i] for i < PARAM_N1.
 * Lanes [PARAM_N1, RS_SUPPORT_VEC_COUNT*64) are padding so each row can be
 * loaded as one or more aligned 128-byte HVX vectors. Padding lanes carry 0,
 * so they accumulate sigma(0) = sigma_0; the final scan only inspects the
 * first PARAM_N1 lanes of `eval`, so those padding entries never reach
 * `error[]`.
 */
static void init_rs_support_powers(void) {
    if (rs_support_powers_ready) {
        return;
    }

    const size_t total_lanes = (size_t)RS_SUPPORT_VEC_COUNT * 64;
    for (size_t lane = 0; lane < total_lanes; ++lane) {
        uint16_t x = (lane < PARAM_N1) ? gf_exp[PARAM_GF_MUL_ORDER - lane] : 0;
        rs_support_powers[0][lane] = 1;
        for (size_t j = 1; j <= PARAM_DELTA; ++j) {
            rs_support_powers[j][lane] = gf_mul(rs_support_powers[j - 1][lane], x);
        }
    }

    rs_support_powers_ready = 1;
}

/**
 * @brief HVX Chien search over the shortened RS support, multi-vector form.
 *
 * Evaluates sigma(x_i) = sum_j sigma_j x_i^j for all PARAM_N1 support points
 * in parallel. For HQC-128 and HQC-192 the support fits in a single 64-lane
 * halfword vector and the inner v-loop trivially unrolls to one vmul+vxor per
 * sigma coefficient (identical code to the original single-vector version).
 * For HQC-256 the support spans two vectors, so each sigma coefficient
 * generates two vmul+vxor pairs.
 *
 * NOTE: For HQC-256, the default decode path uses the additive FFT
 * (compute_roots in fft.c), which is asymptotically cheaper at delta=29
 * (~256*log2(256) = 2048 GF muls) than Chien search (PARAM_N1 * delta =
 * 90 * 29 = 2610 GF muls). This HVX Chien path is offered mainly as a
 * benchmark experiment for HQC-256; the scalar FFT default may still win.
 */
static void compute_roots_hvx(uint8_t *error, const uint16_t *sigma, uint16_t degree) {
    uint16_t eval[RS_SUPPORT_VEC_COUNT * 64] __attribute__((aligned(128)));
    HVX_Vector acc[RS_SUPPORT_VEC_COUNT];
    for (int v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
        acc[v] = Q6_V_vzero();
    }

    init_rs_support_powers();
    memset(error, 0, 1 << PARAM_M);

    for (size_t j = 0; j <= degree; ++j) {
        if (sigma[j] != 0) {
            for (int v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
                HVX_Vector powers = *(const HVX_Vector *)&rs_support_powers[j][v * 64];
                acc[v] = Q6_V_vxor_VV(acc[v],
                                       gf_mul_scalar_by_vec_hvx(sigma[j], powers));
            }
        }
    }

    for (int v = 0; v < RS_SUPPORT_VEC_COUNT; ++v) {
        *(HVX_Vector *)&eval[v * 64] = acc[v];
    }
    for (size_t i = 0; i < PARAM_N1; ++i) {
        error[i] = (uint8_t)(eval[i] == 0);
    }
}
#endif

/**
 * @brief Computes the polynomial z(x)
 *
 * The default path keeps masked PARAM_DELTA loops. In HQC_RS_FAST_NON_CT mode,
 * only coefficients up to the actual locator degree are computed and the
 * remaining coefficients are cleared so later fast stages cannot observe stale
 * stack contents.
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
#if defined(HQC_RS_FAST_NON_CT)
    z[0] = 1;

    for (i = 1; i <= PARAM_DELTA; ++i) {
        z[i] = 0;
    }

    for (i = 1; i <= degree; ++i) {
        z[i] = sigma[i];
    }

    z[1] ^= syndromes[0];

    for (i = 2; i <= degree; ++i) {
        z[i] ^= syndromes[i - 1];

        for (j = 1; j < i; ++j) {
            z[i] ^= gf_mul(sigma[j], syndromes[i - j - 1]);
        }
    }
#else
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
#endif
}

/**
 * @brief Computes the error values
 *
 * The default path keeps fixed-loop masked placement of located errors. The
 * HQC_RS_FAST_NON_CT path first compacts actual error positions, then evaluates
 * z(beta_i^{-1}) and uses the formal derivative sigma'(beta_i^{-1}) as the
 * Forney denominator. This avoids the explicit product over all other located
 * errors and follows the same derivative/odd-locator observation used in
 * Reed-Solomon error-evaluation literature.
 *
 * See @cite lin1983error (Chapter 6 - BCH Codes) for more details.
 *
 * @param[out] error_values Array of PARAM_DELTA elements receiving the error values
 * @param[in] z Array of PARAM_DELTA + 1 elements storing the polynomial z(x)
 * @param[in] error Array storing the error
 */
static void compute_error_values(uint16_t *error_values, const uint16_t *z, const uint8_t *error, const uint16_t *sigma, uint16_t degree) {
#if defined(HQC_RS_FAST_NON_CT)
    uint16_t beta_j[PARAM_DELTA] = {0};
    size_t pos_j[PARAM_DELTA] = {0};
    size_t delta = 0;

    for (size_t i = 0; i < PARAM_N1; ++i) {
        if (error[i] && (delta < PARAM_DELTA)) {
            beta_j[delta] = gf_exp[i];
            pos_j[delta] = i;
            ++delta;
        }
    }

    for (size_t i = 0; i < delta; ++i) {
        uint16_t inverse = gf_inverse(beta_j[i]);
        uint16_t inverse_power_j = 1;
        uint16_t inverse_square = gf_mul(inverse, inverse);
        uint16_t tmp1 = 1;
        uint16_t sigma_derivative = 0;

        for (size_t j = 1; j <= degree; ++j) {
            inverse_power_j = gf_mul(inverse_power_j, inverse);
            tmp1 ^= gf_mul(inverse_power_j, z[j]);
        }

        inverse_power_j = 1;
        for (size_t j = 1; j <= degree; j += 2) {
            sigma_derivative ^= gf_mul(inverse_power_j, sigma[j]);
            inverse_power_j = gf_mul(inverse_power_j, inverse_square);
        }

        error_values[pos_j[i]] = gf_mul(tmp1, gf_mul(beta_j[i], gf_inverse(sigma_derivative)));
    }
#else
    (void)sigma;
    (void)degree;
    uint16_t beta_j[PARAM_DELTA] = {0};
    uint16_t e_j[PARAM_DELTA] = {0};

    uint16_t delta_counter;
    uint16_t delta_real_value;
    uint16_t found;
    uint16_t mask1;
    uint16_t mask2;
    uint16_t tmp1;
    uint16_t tmp2;
    uint16_t inverse;
    uint16_t inverse_power_j;

    // Compute the beta_{j_i} page 31 of the documentation
    delta_counter = 0;
    for (size_t i = 0; i < PARAM_N1; i++) {
        found = 0;
        mask1 = (uint16_t)(-((int32_t)error[i]) >> 31);  // error[i] != 0
        for (size_t j = 0; j < PARAM_DELTA; j++) {
            mask2 = ~((uint16_t)(-((int32_t)j ^ delta_counter) >> 31));  // j == delta_counter
            beta_j[j] += mask1 & mask2 & gf_exp[i];
            found += mask1 & mask2 & 1;
        }
        delta_counter += found;
    }
    delta_real_value = delta_counter;

    // Compute the e_{j_i} page 31 of the documentation
    for (size_t i = 0; i < PARAM_DELTA; ++i) {
        tmp1 = 1;
        tmp2 = 1;
        inverse = gf_inverse(beta_j[i]);
        inverse_power_j = 1;

        for (size_t j = 1; j <= PARAM_DELTA; ++j) {
            inverse_power_j = gf_mul(inverse_power_j, inverse);
            tmp1 ^= gf_mul(inverse_power_j, z[j]);
        }
        for (size_t k = 1; k < PARAM_DELTA; ++k) {
            tmp2 = gf_mul(tmp2, (1 ^ gf_mul(inverse, beta_j[(i + k) % PARAM_DELTA])));
        }
        mask1 = (uint16_t)(((int16_t)i - delta_real_value) >> 15);  // i < delta_real_value
        e_j[i] = mask1 & gf_mul(tmp1, gf_inverse(tmp2));
    }

    // Place the delta e_{j_i} values at the right coordinates of the output vector
    delta_counter = 0;
    for (size_t i = 0; i < PARAM_N1; ++i) {
        found = 0;
        mask1 = (uint16_t)(-((int32_t)error[i]) >> 31);  // error[i] != 0
        for (size_t j = 0; j < PARAM_DELTA; j++) {
            mask2 = ~((uint16_t)(-((int32_t)j ^ delta_counter) >> 31));  // j == delta_counter
            error_values[i] += mask1 & mask2 & e_j[j];
            found += mask1 & mask2 & 1;
        }
        delta_counter += found;
    }
#endif
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
 * -# Find the roots of σ(x), using shortened-support Chien search by default,
 *    additive FFT only when HQC_RS_ROOTS_FFT=1, or HVX Chien when
 *    HQC_RS_ROOTS_HVX=1 on Hexagon.
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
    uint8_t error[1 << PARAM_M] = {0};
    uint16_t z[PARAM_N1] = {0};
    uint16_t error_values[PARAM_N1] = {0};
    uint16_t deg;

    // Copy the vector in an array of bytes
    memcpy(cdw_bytes, cdw, PARAM_N1);

    // Calculate the 2*PARAM_DELTA syndromes
    compute_syndromes(syndromes, cdw_bytes);

    // Compute the error locator polynomial sigma
    // Sigma's degree is at most PARAM_DELTA. The larger buffer is kept for
    // compatibility with the optional additive-FFT root finder.
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
