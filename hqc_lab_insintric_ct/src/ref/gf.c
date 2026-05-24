/**
 * @file gf.c
 * @brief GF(2^8) arithmetic for the HQC-128 CT intrinsic lab.
 *
 * This build uses the fixed-flow hardware-style xtime/xor multiplier only.
 * Data-dependent GF multiplication lookup tables are intentionally absent.
 */

#include "gf.h"
#include <stdint.h>
#include "parameters.h"

static uint16_t gf_reduce(uint16_t x);
static uint16_t gf_mul_hwstyle(uint16_t a, uint16_t b);

/**
 * @brief Generates exp and log lookup tables of GF(2^8).
 *
 * @note   this function is not used in the code; it was used to generate
 *         the lookup table for GF(2^8).
 *
 * The logarithm of 0 is defined as 2^8 by convention. <br>
 * The last two elements of the exp table are needed by the gf_mul function from gf_lutmul.c
 * (for example if both elements to multiply are zero).
 * @param[out] exp Array of size 2^8 + 2 receiving the powers of the primitive element
 * @param[out] log Array of size 2^8 receiving the logarithms of the elements of GF(2^m)
 * @param[in] m Parameter of Galois field GF(2^m)
 */
void gf_generate(uint16_t *exp, uint16_t *log, const int16_t m) {
    uint16_t elt = 1;
    uint16_t alpha = 2;  // primitive element of GF(2^8)
    uint16_t gf_poly = PARAM_GF_POLY;

    for (size_t i = 0; i < (1U << m) - 1; ++i) {
        exp[i] = elt;
        log[elt] = i;

        elt *= alpha;
        if (elt >= 1 << m)
            elt ^= gf_poly;
    }

    exp[(1 << m) - 1] = 1;
    exp[1 << m] = 2;
    exp[(1 << m) + 1] = 4;
    log[0] = 0;  // by convention
}

/**
 * @brief Feedback bit positions used for modular reduction by PARAM_GF_POLY = 0x11D.
 *
 * These values are derived from the binary form of the polynomial:
 *     0x11D = 0b100011101 → bits set at positions: 8, 4, 3, 1, 0
 *
 * To reduce a polynomial modulo this irreducible polynomial:
 * - Bit 8 (the leading term) is handled via shifting: mod = x >> 8
 * - Bit 0 (constant term) is handled by the initial XOR
 *
 * The remaining set bits at positions 4, 3, and 2 define where the shifted
 * high bits (mod) must be XORed back into the result. These represent the
 * feedback positions used during reduction.
 */
static const uint8_t gf_reduction_taps[] = {4, 3, 2};

/**
 * @brief Reduce a polynomial modulo PARAM_GF_POLY in GF(2^8).
 *
 * This function performs modular reduction of a 16-bit polynomial `x`
 * by the irreducible polynomial PARAM_GF_POLY = 0x11D
 * (i.e., x⁸ + x⁴ + x³ + x + 1), used in GF(2^8).
 *
 * It assumes the input polynomial has degree ≤ 14 and uses a fixed
 * number of reduction steps and fixed feedback tap positions
 * ({4, 3, 2}) to produce a result of degree < 8.
 *
 * @param x 16-bit input polynomial to reduce (deg(x) ≤ 14)
 * @return Reduced 8-bit polynomial modulo PARAM_GF_POLY (deg(x) < 8)
 */
uint16_t gf_reduce(uint16_t x) {
    uint64_t mod;
    const int reduction_steps = 2;            // For deg(x) = 2 * (8 - 1) = 14, reduce twice to bring degree < 8
    const size_t gf_reduction_tap_count = 3;  // Number of feedback positions

    for (int i = 0; i < reduction_steps; ++i) {
        mod = x >> PARAM_M;       // Extract upper bits
        x &= (1 << PARAM_M) - 1;  // Keep lower bits
        x ^= mod;                 // Pre-XOR with no shift

        uint16_t z1 = 0;
        for (size_t j = gf_reduction_tap_count; j; --j) {
            uint16_t z2 = gf_reduction_taps[j - 1];
            uint16_t dist = z2 - z1;
            mod <<= dist;
            x ^= mod;
            z1 = z2;
        }
    }

    return x;
}

static inline uint16_t gf_xtime(uint16_t x) {
    uint16_t carry = x >> 7;
    return (uint16_t)(((x << 1) ^ (0x1d & (uint16_t)-carry)) & 0xff);
}

static uint16_t gf_mul_hwstyle(uint16_t a, uint16_t b) {
    uint16_t acc = 0;
    a &= 0xff;
    b &= 0xff;

#define GF_MUL_HWSTYLE_STEP(bit)             \
    do {                                     \
        uint16_t bit_mask = (uint16_t)-((b >> (bit)) & 1); \
        acc = gf_xtime(acc);                 \
        acc ^= a & bit_mask;                 \
    } while (0)

    GF_MUL_HWSTYLE_STEP(7);
    GF_MUL_HWSTYLE_STEP(6);
    GF_MUL_HWSTYLE_STEP(5);
    GF_MUL_HWSTYLE_STEP(4);
    GF_MUL_HWSTYLE_STEP(3);
    GF_MUL_HWSTYLE_STEP(2);
    GF_MUL_HWSTYLE_STEP(1);
    GF_MUL_HWSTYLE_STEP(0);

#undef GF_MUL_HWSTYLE_STEP

    return acc;
}

/**
 * Multiplies two elements of GF(2^GF_M).
 * @returns the product a*b
 * @param[in] a Element of GF(2^GF_M)
 * @param[in] b Element of GF(2^GF_M)
 */
uint16_t gf_mul(uint16_t a, uint16_t b) {
    return gf_mul_hwstyle(a, b);
}

/**
 * Squares an element of GF(2^GF_M).
 * @returns a^2
 * @param[in] a Element of GF(2^GF_M)
 */
uint16_t gf_square(uint16_t a) {
    uint32_t b = a;
    uint32_t s = b & 1;
    for (size_t i = 1; i < PARAM_M; ++i) {
        b <<= 1;
        s ^= b & (1 << 2 * i);
    }

    return gf_reduce(s);
}

/**
 * Computes the inverse of an element of GF(2^8),
 * using the addition chain 1 2 3 4 7 11 15 30 60 120 127 254
 * @returns the inverse of a
 * @param[in] a Element of GF(2^GF_M)
 */
uint16_t gf_inverse(uint16_t a) {
    uint16_t inv = a;
    uint16_t tmp1, tmp2;

    inv = gf_square(a);       /* a^2 */
    tmp1 = gf_mul(inv, a);    /* a^3 */
    inv = gf_square(inv);     /* a^4 */
    tmp2 = gf_mul(inv, tmp1); /* a^7 */
    tmp1 = gf_mul(inv, tmp2); /* a^11 */
    inv = gf_mul(tmp1, inv);  /* a^15 */
    inv = gf_square(inv);     /* a^30 */
    inv = gf_square(inv);     /* a^60 */
    inv = gf_square(inv);     /* a^120 */
    inv = gf_mul(inv, tmp2);  /* a^127 */
    inv = gf_square(inv);     /* a^254 */
    return inv;
}
