/**
 * @file parameters.h
 * @brief Minimal HQC-256 parameters for concatenated RS/RM encode-decode.
 */

#ifndef HQC_PARAMETERS_H
#define HQC_PARAMETERS_H

#define CEIL_DIVIDE(a, b) (((a) / (b)) + ((a) % (b) == 0 ? 0 : 1)) /*!< Divide a by b and ceil the result*/

#define PARAM_N1                    90          ///< Reed-Solomon code length in bytes
#define PARAM_N2                    640         ///< Duplicated Reed-Muller code length in bits per RS byte
#define PARAM_N1N2                  57600       ///< Define the length in bits of the concatenated code

#define VEC_K_SIZE_BYTES            PARAM_K                     ///< Size of array to store PARAM_K bits in bytes
#define VEC_N1_SIZE_BYTES           PARAM_N1                    ///< Size of array to store PARAM_N1 bits in bytes
#define VEC_N1N2_SIZE_BYTES         CEIL_DIVIDE(PARAM_N1N2, 8)  ///< Size of array to store PARAM_N1N2 bits in bytes

#define VEC_N1_SIZE_64              CEIL_DIVIDE(PARAM_N1, 8)    ///< Size of array to store PARAM_N1 bits in 64-bit words
#define VEC_N1N2_SIZE_64            CEIL_DIVIDE(PARAM_N1N2, 64) ///< Size of array to store PARAM_N1N2 bits in 64-bit words

#define PARAM_DELTA                 29          ///< Reed-Solomon error-correcting capacity
#define PARAM_M                     8           ///< Define the degree m of the Galois field GF(2^m)
#define PARAM_GF_POLY               0x11D       ///< Generator polynomial of GF(2^PARAM_M) in hexadecimal form
#define PARAM_GF_MUL_ORDER          255         ///< Size of the multiplicative group of GF(2^PARAM_M)
#define PARAM_K                     32          ///< Reed-Solomon message length in bytes
#define PARAM_G                     59          ///< Reed-Solomon generator polynomial size (2*PARAM_DELTA + 1)
#define PARAM_FFT                   5           ///< Exponent for additive FFT (2^PARAM_FFT points)

/* Coefficients of the Reed-Solomon generator polynomial g3(x). Single line on purpose so the macro
 * can be expanded directly via {RS_POLY_COEFS} in array initializers. */
#define RS_POLY_COEFS 49, 167, 49, 39, 200, 121, 124, 91, 240, 63, 148, 71, 150, 123, 87, 101, 32, 215, 159, 71, 201, 115, 97, 210, 186, 183, 141, 217, 123, 12, 31, 243, 180, 219, 152, 239, 99, 141, 4, 246, 191, 144, 8, 232, 47, 27, 141, 178, 130, 64, 124, 47, 39, 188, 216, 48, 199, 187, 1

#endif // HQC_PARAMETERS_H
