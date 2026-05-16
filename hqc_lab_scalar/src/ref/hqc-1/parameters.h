/**
* @file parameters.h
* @brief Minimal HQC-128 parameters for concatenated RS/RM encode-decode.
 */

#ifndef HQC_PARAMETERS_H
#define HQC_PARAMETERS_H

#define CEIL_DIVIDE(a, b) (((a) / (b)) + ((a) % (b) == 0 ? 0 : 1)) /*!< Divide a by b and ceil the result*/

#define PARAM_N1                    46          ///< Reed-Solomon code length in bytes
#define PARAM_N2                    384         ///< Duplicated Reed-Muller code length in bits per RS byte
#define PARAM_N1N2                  17664       ///< Define the length in bits of the concatenated code

#define VEC_K_SIZE_BYTES            PARAM_K                     ///< Size of array to store PARAM_K bits in bytes
#define VEC_N1_SIZE_BYTES           PARAM_N1                    ///< Size of array to store PARAM_N1 bits in bytes
#define VEC_N1N2_SIZE_BYTES         CEIL_DIVIDE(PARAM_N1N2, 8)  ///< Size of array to store PARAM_N1N2 bits in bytes

#define VEC_N1_SIZE_64              CEIL_DIVIDE(PARAM_N1, 8)    ///< Size of array to store PARAM_N1 bits in 64-bit words
#define VEC_N1N2_SIZE_64            CEIL_DIVIDE(PARAM_N1N2, 64) ///< Size of array to store PARAM_N1N2 bits in 64-bit words

#define PARAM_DELTA                 15          ///< Reed-Solomon error-correcting capacity
#define PARAM_M                     8           ///< Define the degree m of the Galois field GF(2^m)
#define PARAM_GF_POLY               0x11D       ///< Generator polynomial of GF(2^PARAM_M) in hexadecimal form
#define PARAM_GF_MUL_ORDER          255         ///< Size of the multiplicative group of GF(2^PARAM_M)
#define PARAM_K                     16          ///< Reed-Solomon message length in bytes
#define PARAM_G                     31          ///< Reed-Solomon generator polynomial size
#define PARAM_FFT                   4           ///< Exponent for additive FFT (2^PARAM_FFT points)

#define RS_POLY_COEFS                                                                                                \
	    89,  69, 153, 116, 176, 117, 111, 75,  73, 233, 242, 233, 65,  210, 21,  139, 103, 173, 67,  118,                   \
	    105, 210, 174, 110, 74,  69, 228, 82,  255, 181, 1                                                     ///< Coefficients of the Reed-Solomon generator polynomial

#endif // HQC_PARAMETERS_H
