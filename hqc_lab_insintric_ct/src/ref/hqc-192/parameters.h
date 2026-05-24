/**
 * @file parameters.h
 * @brief Minimal HQC-192 parameters for concatenated RS/RM encode-decode.
 */

#ifndef HQC_PARAMETERS_H
#define HQC_PARAMETERS_H

#define CEIL_DIVIDE(a, b) (((a) / (b)) + ((a) % (b) == 0 ? 0 : 1)) /*!< Divide a by b and ceil the result*/

#define PARAM_N1                    56          ///< Reed-Solomon code length in bytes
#define PARAM_N2                    640         ///< Duplicated Reed-Muller code length in bits per RS byte
#define PARAM_N1N2                  35840       ///< Define the length in bits of the concatenated code

#define VEC_K_SIZE_BYTES            PARAM_K                     ///< Size of array to store PARAM_K bits in bytes
#define VEC_N1_SIZE_BYTES           PARAM_N1                    ///< Size of array to store PARAM_N1 bits in bytes
#define VEC_N1N2_SIZE_BYTES         CEIL_DIVIDE(PARAM_N1N2, 8)  ///< Size of array to store PARAM_N1N2 bits in bytes

#define VEC_N1_SIZE_64              CEIL_DIVIDE(PARAM_N1, 8)    ///< Size of array to store PARAM_N1 bits in 64-bit words
#define VEC_N1N2_SIZE_64            CEIL_DIVIDE(PARAM_N1N2, 64) ///< Size of array to store PARAM_N1N2 bits in 64-bit words

#define PARAM_DELTA                 16          ///< Reed-Solomon error-correcting capacity
#define PARAM_M                     8           ///< Define the degree m of the Galois field GF(2^m)
#define PARAM_GF_POLY               0x11D       ///< Generator polynomial of GF(2^PARAM_M) in hexadecimal form
#define PARAM_GF_MUL_ORDER          255         ///< Size of the multiplicative group of GF(2^PARAM_M)
#define PARAM_K                     24          ///< Reed-Solomon message length in bytes
#define PARAM_G                     33          ///< Reed-Solomon generator polynomial size (2*PARAM_DELTA + 1)
#define PARAM_FFT                   5           ///< Exponent for additive FFT (2^PARAM_FFT points)

/* Coefficients of the Reed-Solomon generator polynomial g2(x). Single line on purpose so the macro
 * can be expanded directly via {RS_POLY_COEFS} in array initializers. */
#define RS_POLY_COEFS 45, 216, 239, 24, 253, 104, 27, 40, 107, 50, 163, 210, 227, 134, 224, 158, 119, 13, 158, 1, 238, 164, 82, 43, 15, 232, 246, 142, 50, 189, 29, 232, 1

#endif // HQC_PARAMETERS_H
