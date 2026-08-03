#ifndef _HQC_SKEL_H
#define _HQC_SKEL_H
#include "hqc.h"

#include <string.h>
#ifndef _WIN32
#include "HAP_farf.h"
#endif //_WIN32 for HAP_farf
#ifndef _ALLOCATOR_H
#define _ALLOCATOR_H

#include <stdlib.h>
#include <stdint.h>

typedef struct _heap _heap;
struct _heap {
   _heap* pPrev;
   const char* loc;
   uint64_t buf;
};

typedef struct _allocator {
   _heap* pheap;
   uint8_t* stack;
   uint8_t* stackEnd;
   int nSize;
} _allocator;

_ATTRIBUTE_UNUSED
static __inline int _heap_alloc(_heap** ppa, const char* loc, size_t size, void** ppbuf) {
   _heap* pn = 0;
   pn = MALLOC(size + sizeof(_heap) - sizeof(uint64_t));
   if(pn != 0) {
      pn->pPrev = *ppa;
      pn->loc = loc;
      *ppa = pn;
      *ppbuf = (void*)&(pn->buf);
      return 0;
   } else {
      return -1;
   }
}
#define _ALIGN_SIZE(x, y) (((x) + (y-1)) & ~(y-1))

_ATTRIBUTE_UNUSED
static __inline int _allocator_alloc(_allocator* me,
                                    const char* loc,
                                    size_t size,
                                    unsigned int al,
                                    void** ppbuf) {
   if(size < 0) {
      return -1;
   } else if (size == 0) {
      *ppbuf = 0;
      return 0;
   }
   if((_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + size) < (uintptr_t)me->stack + (size_t)me->nSize) {
      *ppbuf = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al);
      me->stackEnd = (uint8_t*)_ALIGN_SIZE((uintptr_t)me->stackEnd, al) + size;
      return 0;
   } else {
      return _heap_alloc(&me->pheap, loc, size, ppbuf);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_deinit(_allocator* me) {
   _heap* pa = me->pheap;
   while(pa != 0) {
      _heap* pn = pa;
      const char* loc = pn->loc;
      (void)loc;
      pa = pn->pPrev;
      FREE(pn);
   }
}

_ATTRIBUTE_UNUSED
static __inline void _allocator_init(_allocator* me, uint8_t* stack, int stackSize) {
   me->stack =  stack;
   me->stackEnd =  stack + stackSize;
   me->nSize = stackSize;
   me->pheap = 0;
}


#endif // _ALLOCATOR_H

#ifndef SLIM_H
#define SLIM_H

#include <stdint.h>

//a C data structure for the idl types that can be used to implement
//static and dynamic language bindings fairly efficiently.
//
//the goal is to have a minimal ROM and RAM footprint and without
//doing too many allocations.  A good way to package these things seemed
//like the module boundary, so all the idls within  one module can share
//all the type references.


#define PARAMETER_IN       0x0
#define PARAMETER_OUT      0x1
#define PARAMETER_INOUT    0x2
#define PARAMETER_ROUT     0x3
#define PARAMETER_INROUT   0x4

//the types that we get from idl
#define TYPE_OBJECT             0x0
#define TYPE_INTERFACE          0x1
#define TYPE_PRIMITIVE          0x2
#define TYPE_ENUM               0x3
#define TYPE_STRING             0x4
#define TYPE_WSTRING            0x5
#define TYPE_STRUCTURE          0x6
#define TYPE_UNION              0x7
#define TYPE_ARRAY              0x8
#define TYPE_SEQUENCE           0x9

//these require the pack/unpack to recurse
//so it's a hint to those languages that can optimize in cases where
//recursion isn't necessary.
#define TYPE_COMPLEX_STRUCTURE  (0x10 | TYPE_STRUCTURE)
#define TYPE_COMPLEX_UNION      (0x10 | TYPE_UNION)
#define TYPE_COMPLEX_ARRAY      (0x10 | TYPE_ARRAY)
#define TYPE_COMPLEX_SEQUENCE   (0x10 | TYPE_SEQUENCE)


typedef struct Type Type;

#define INHERIT_TYPE\
   int32_t nativeSize;                /*in the simple case its the same as wire size and alignment*/\
   union {\
      struct {\
         const uintptr_t         p1;\
         const uintptr_t         p2;\
      } _cast;\
      struct {\
         uint32_t  iid;\
         uint32_t  bNotNil;\
      } object;\
      struct {\
         const Type  *arrayType;\
         int32_t      nItems;\
      } array;\
      struct {\
         const Type *seqType;\
         int32_t      nMaxLen;\
      } seqSimple; \
      struct {\
         uint32_t bFloating;\
         uint32_t bSigned;\
      } prim; \
      const SequenceType* seqComplex;\
      const UnionType  *unionType;\
      const StructType *structType;\
      int32_t         stringMaxLen;\
      uint8_t        bInterfaceNotNil;\
   } param;\
   uint8_t    type;\
   uint8_t    nativeAlignment\

typedef struct UnionType UnionType;
typedef struct StructType StructType;
typedef struct SequenceType SequenceType;
struct Type {
   INHERIT_TYPE;
};

struct SequenceType {
   const Type *         seqType;
   uint32_t               nMaxLen;
   uint32_t               inSize;
   uint32_t               routSizePrimIn;
   uint32_t               routSizePrimROut;
};

//byte offset from the start of the case values for
//this unions case value array.  it MUST be aligned
//at the alignment requrements for the descriptor
//
//if negative it means that the unions cases are
//simple enumerators, so the value read from the descriptor
//can be used directly to find the correct case
typedef union CaseValuePtr CaseValuePtr;
union CaseValuePtr {
   const uint8_t*   value8s;
   const uint16_t*  value16s;
   const uint32_t*  value32s;
   const uint64_t*  value64s;
};

//these are only used in complex cases
//so I pulled them out of the type definition as references to make
//the type smaller
struct UnionType {
   const Type           *descriptor;
   uint32_t               nCases;
   const CaseValuePtr   caseValues;
   const Type * const   *cases;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
   uint8_t                inCaseAlignment;
   uint8_t                routCaseAlignmentPrimIn;
   uint8_t                routCaseAlignmentPrimROut;
   uint8_t                nativeCaseAlignment;
   uint8_t              bDefaultCase;
};

struct StructType {
   uint32_t               nMembers;
   const Type * const   *members;
   int32_t               inSize;
   int32_t               routSizePrimIn;
   int32_t               routSizePrimROut;
   uint8_t                inAlignment;
   uint8_t                routAlignmentPrimIn;
   uint8_t                routAlignmentPrimROut;
};

typedef struct Parameter Parameter;
struct Parameter {
   INHERIT_TYPE;
   uint8_t    mode;
   uint8_t  bNotNil;
};

#define SLIM_IFPTR32(is32,is64) (sizeof(uintptr_t) == 4 ? (is32) : (is64))
#define SLIM_SCALARS_IS_DYNAMIC(u) (((u) & 0x00ffffff) == 0x00ffffff)

typedef struct Method Method;
struct Method {
   uint32_t                    uScalars;            //no method index
   int32_t                     primInSize;
   int32_t                     primROutSize;
   int                         maxArgs;
   int                         numParams;
   const Parameter * const     *params;
   uint8_t                       primInAlignment;
   uint8_t                       primROutAlignment;
};

typedef struct Interface Interface;

struct Interface {
   int                            nMethods;
   const Method  * const          *methodArray;
   int                            nIIds;
   const uint32_t                   *iids;
   const uint16_t*                  methodStringArray;
   const uint16_t*                  methodStrings;
   const char*                    strings;
};


#endif //SLIM_H


#ifndef _HQC_SLIM_H
#define _HQC_SLIM_H
#include <stdint.h>

#ifndef __QAIC_SLIM
#define __QAIC_SLIM(ff) ff
#endif
#ifndef __QAIC_SLIM_EXPORT
#define __QAIC_SLIM_EXPORT
#endif

static const Type types[1];
static const Type types[1] = {{0x1,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x1}};
static const Parameter parameters[7] = {{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)0x0,0}}, 4,SLIM_IFPTR32(0x4,0x8),0,0},{SLIM_IFPTR32(0x4,0x8),{{(const uintptr_t)0xdeadc0de,(const uintptr_t)0}}, 0,SLIM_IFPTR32(0x4,0x8),3,0},{SLIM_IFPTR32(0x4,0x8),{{(const uintptr_t)0xdeadc0de,(const uintptr_t)0}}, 0,SLIM_IFPTR32(0x4,0x8),0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,0,0},{0x4,{{(const uintptr_t)0,(const uintptr_t)1}}, 2,0x4,3,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),0,0},{SLIM_IFPTR32(0x8,0x10),{{(const uintptr_t)&(types[0]),(const uintptr_t)0x0}}, 9,SLIM_IFPTR32(0x4,0x8),3,0}};
static const Parameter* const parameterArrays[21] = {(&(parameters[5])),(&(parameters[6])),(&(parameters[3])),(&(parameters[3])),(&(parameters[3])),(&(parameters[3])),(&(parameters[3])),(&(parameters[4])),(&(parameters[4])),(&(parameters[4])),(&(parameters[4])),(&(parameters[5])),(&(parameters[6])),(&(parameters[3])),(&(parameters[4])),(&(parameters[5])),(&(parameters[3])),(&(parameters[4])),(&(parameters[0])),(&(parameters[1])),(&(parameters[2]))};
static const Method methods[10] = {{REMOTE_SCALARS_MAKEX(0,0,0x2,0x0,0x0,0x1),0x4,0x0,2,2,(&(parameterArrays[18])),0x4,0x1},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x0,0x1,0x0),0x0,0x0,1,1,(&(parameterArrays[20])),0x1,0x0},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x4,0x4,2,2,(&(parameterArrays[6])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x0,0x1,0x0,0x0),0x0,0xc,3,3,(&(parameterArrays[7])),0x1,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x4,0x10,5,5,(&(parameterArrays[6])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x1,0x0,0x0),0x8,0xc,5,5,(&(parameterArrays[5])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x1,0x0,0x0),0x8,0x4,4,3,(&(parameterArrays[15])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x1,0x2,0x0,0x0),0x8,0x4,5,3,(&(parameterArrays[12])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x2,0x0,0x0),0xc,0x4,7,4,(&(parameterArrays[11])),0x4,0x4},{REMOTE_SCALARS_MAKEX(0,0,0x2,0x2,0x0,0x0),0x1c,0x10,14,11,(&(parameterArrays[0])),0x4,0x4}};
static const Method* const methodArrays[11] = {&(methods[0]),&(methods[1]),&(methods[2]),&(methods[3]),&(methods[4]),&(methods[4]),&(methods[5]),&(methods[6]),&(methods[7]),&(methods[8]),&(methods[9])};
static const char strings[323] = "total_rs_symbol_errors\0decode_buffer_bench\0decode_bench_count\0codeword_stride\0message_stride\0codeword_count\0substage_bench\0payload_inout\0total_decodes\0decode_count\0decode_bench\0mode_status\0payload_out\0payload_in\0decode_one\0codewords\0total_ops\0dsp_mode\0messages\0checksum\0output\0passed\0input\0stage\0iters\0close\0ping\0open\0uri\0";
static const uint16_t methodStrings[55] = {23,223,252,296,93,62,78,243,137,261,277,177,108,290,296,233,261,277,43,151,137,0,261,277,164,296,137,0,261,277,123,284,270,296,261,189,270,296,261,201,284,296,261,212,6,261,277,308,296,261,313,318,41,302,41};
static const uint16_t methodStringsArrays[11] = {50,53,47,43,24,18,12,39,35,30,0};
__QAIC_SLIM_EXPORT const Interface __QAIC_SLIM(hqc_slim) = {11,&(methodArrays[0]),0,0,&(methodStringsArrays [0]),methodStrings,strings};
#endif //_HQC_SLIM_H
extern int adsp_mmap_fd_getinfo(int, uint32_t *);
#ifdef __cplusplus
extern "C" {
#endif
_ATTRIBUTE_VISIBILITY uint32_t hqc_skel_handle_invoke_qaic_version = 10049;
_ATTRIBUTE_VISIBILITY char hqc_skel_handle_invoke_uri[57+1]="file:///libhqc_skel.so?hqc_skel_handle_invoke&_modver=1.0";
static __inline int _skel_method(int (*_pfn)(remote_handle64, const unsigned char*, int, unsigned char*, int, int, int, int, int, int, int*, int*, int*, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _rout1[1] = {0};
   uint32_t _rout1Len[1] = {0};
   uint32_t _in2[1] = {0};
   uint32_t _in3[1] = {0};
   uint32_t _in4[1] = {0};
   uint32_t _in5[1] = {0};
   uint32_t _in6[1] = {0};
   uint32_t _rout7[1] = {0};
   uint32_t _rout8[1] = {0};
   uint32_t _rout9[1] = {0};
   uint32_t _rout10[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((2 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 28);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 16);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _QAIC_ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _COPY(_rout1Len, 0, _primIn, 4, 4);
   _praROut = (_praIn + _numIn[0] + 1);
   _QAIC_ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout1Len[0]));
   _rout1[0] = _praROut[0].buf.pv;
   _COPY(_in2, 0, _primIn, 8, 4);
   _COPY(_in3, 0, _primIn, 12, 4);
   _COPY(_in4, 0, _primIn, 16, 4);
   _COPY(_in5, 0, _primIn, 20, 4);
   _COPY(_in6, 0, _primIn, 24, 4);
   _TRY(_nErr, _pfn(_h, (const unsigned char*)*_in0, (int)*_in0Len, (unsigned char*)*_rout1, (int)*_rout1Len, (int)*_in2, (int)*_in3, (int)*_in4, (int)*_in5, (int)*_in6, (int*)_rout7, (int*)_rout8, (int*)_rout9, (int*)_rout10));
   _COPY(_primROut, 0, _rout7, 0, 4);
   _COPY(_primROut, 4, _rout8, 0, 4);
   _COPY(_primROut, 8, _rout9, 0, 4);
   _COPY(_primROut, 12, _rout10, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_1(int (*_pfn)(remote_handle64, const unsigned char*, int, unsigned char*, int, int, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   char* _rout1[1] = {0};
   uint32_t _rout1Len[1] = {0};
   uint32_t _in2[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((2 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 12);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _QAIC_ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _COPY(_rout1Len, 0, _primIn, 4, 4);
   _praROut = (_praIn + _numIn[0] + 1);
   _QAIC_ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout1Len[0]));
   _rout1[0] = _praROut[0].buf.pv;
   _COPY(_in2, 0, _primIn, 8, 4);
   _TRY(_nErr, _pfn(_h, (const unsigned char*)*_in0, (int)*_in0Len, (unsigned char*)*_rout1, (int)*_rout1Len, (int)*_in2, (int*)_rout3));
   _COPY(_primROut, 0, _rout3, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_2(int (*_pfn)(remote_handle64, unsigned char*, int, int, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _rout0[1] = {0};
   uint32_t _rout0Len[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   remote_arg* _praROut = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((1 + 2) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_rout0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _praROut = (_praIn + _numIn[0] + 1);
   _QAIC_ASSERT(_nErr, ((_praROut[0].buf.nLen / 1)) >= (size_t)(_rout0Len[0]));
   _rout0[0] = _praROut[0].buf.pv;
   _COPY(_in1, 0, _primIn, 4, 4);
   _TRY(_nErr, _pfn(_h, (unsigned char*)*_rout0, (int)*_rout0Len, (int)*_in1, (int*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_3(int (*_pfn)(remote_handle64, const unsigned char*, int, int, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((2 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _QAIC_ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _COPY(_in1, 0, _primIn, 4, 4);
   _TRY(_nErr, _pfn(_h, (const unsigned char*)*_in0, (int)*_in0Len, (int)*_in1, (int*)_rout2));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_4(int (*_pfn)(remote_handle64, int, int, int*, int*, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _in1[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t _rout4[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 8);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 12);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _COPY(_in1, 0, _primIn, 4, 4);
   _TRY(_nErr, _pfn(_h, (int)*_in0, (int)*_in1, (int*)_rout2, (int*)_rout3, (int*)_rout4));
   _COPY(_primROut, 0, _rout2, 0, 4);
   _COPY(_primROut, 4, _rout3, 0, 4);
   _COPY(_primROut, 8, _rout4, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_5(int (*_pfn)(remote_handle64, int, int*, int*, int*, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _rout1[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t _rout3[1] = {0};
   uint32_t _rout4[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 16);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn(_h, (int)*_in0, (int*)_rout1, (int*)_rout2, (int*)_rout3, (int*)_rout4));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _COPY(_primROut, 4, _rout2, 0, 4);
   _COPY(_primROut, 8, _rout3, 0, 4);
   _COPY(_primROut, 12, _rout4, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_6(int (*_pfn)(remote_handle64, int*, int*, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _rout0[1] = {0};
   uint32_t _rout1[1] = {0};
   uint32_t _rout2[1] = {0};
   uint32_t* _primROut= 0;
   int _numIn[1] = {0};
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((0 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 0);
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 0)].buf.nLen >= 12);
   _primROut = _pra[(_numIn[0] + 0)].buf.pv;
   _TRY(_nErr, _pfn(_h, (int*)_rout0, (int*)_rout1, (int*)_rout2));
   _COPY(_primROut, 0, _rout0, 0, 4);
   _COPY(_primROut, 4, _rout1, 0, 4);
   _COPY(_primROut, 8, _rout2, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_7(int (*_pfn)(remote_handle64, int, int*), remote_handle64 _h, uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   uint32_t _in0[1] = {0};
   uint32_t _rout1[1] = {0};
   uint32_t* _primIn= 0;
   int _numIn[1] = {0};
   uint32_t* _primROut= 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((1 + 1) + (((0 + 0) + 0) + 0))) <= _praEnd);
   _numIn[0] = (REMOTE_SCALARS_INBUFS(_sc) - 1);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _QAIC_ASSERT(_nErr, _pra[(_numIn[0] + 1)].buf.nLen >= 4);
   _primROut = _pra[(_numIn[0] + 1)].buf.pv;
   _COPY(_in0, 0, _primIn, 0, 4);
   _TRY(_nErr, _pfn(_h, (int)*_in0, (int*)_rout1));
   _COPY(_primROut, 0, _rout1, 0, 4);
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_8(int (*_pfn)(remote_handle64), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   remote_handle64 _in0[1] = {0};
   remote_arg* _praRHandleIn = _pra + REMOTE_SCALARS_INBUFS(_sc) +  REMOTE_SCALARS_OUTBUFS(_sc);
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==1);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, (_pra + ((0 + 0) + (((1 + 0) + 0) + 0))) <= _praEnd);
   _COPY(_in0, 0, &(_praRHandleIn[0].h64), 0, sizeof(remote_handle64));
   _TRY(_nErr, _pfn((remote_handle64)*_in0));
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
static __inline int _skel_method_9(int (*_pfn)(const char*, remote_handle64*), uint32_t _sc, remote_arg* _pra) {
   remote_arg* _praEnd = 0;
   char* _in0[1] = {0};
   uint32_t _in0Len[1] = {0};
   remote_handle64 _rout1[1] = {0};
   uint32_t* _primIn= 0;
   remote_arg* _praRHandleROut = _pra + REMOTE_SCALARS_INBUFS(_sc) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) ;
   remote_arg* _praIn = 0;
   int _nErr = 0;
   _praEnd = ((_pra + REMOTE_SCALARS_INBUFS(_sc)) + REMOTE_SCALARS_OUTBUFS(_sc) + REMOTE_SCALARS_INHANDLES(_sc) + REMOTE_SCALARS_OUTHANDLES(_sc));
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INBUFS(_sc)==2);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTBUFS(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_INHANDLES(_sc)==0);
   _QAIC_ASSERT(_nErr, REMOTE_SCALARS_OUTHANDLES(_sc)==1);
   _QAIC_ASSERT(_nErr, (_pra + ((2 + 0) + (((0 + 1) + 0) + 0))) <= _praEnd);
   _QAIC_ASSERT(_nErr, _pra[0].buf.nLen >= 4);
   _primIn = _pra[0].buf.pv;
   _COPY(_in0Len, 0, _primIn, 0, 4);
   _praIn = (_pra + 1);
   _QAIC_ASSERT(_nErr, ((_praIn[0].buf.nLen / 1)) >= (size_t)(_in0Len[0]));
   _in0[0] = _praIn[0].buf.pv;
   _QAIC_ASSERT(_nErr, (_in0Len[0] > 0) && (_in0[0][(_in0Len[0] - 1)] == 0));
   _TRY(_nErr, _pfn((const char*)*_in0, (remote_handle64*)_rout1));
   _COPY(&(_praRHandleROut[0].h64), 0, _rout1, 0, sizeof(remote_handle64));
   _QAIC_CATCH(_nErr) {}
   return _nErr;
}
__QAIC_SKEL_EXPORT int __QAIC_SKEL(hqc_skel_handle_invoke)(remote_handle64 _h, uint32_t _sc, remote_arg* _pra) __QAIC_SKEL_ATTRIBUTE {
   switch(REMOTE_SCALARS_METHOD(_sc)){
      case 0:
      return _skel_method_9(__QAIC_IMPL(hqc_open), _sc, _pra);
      case 1:
      return _skel_method_8(__QAIC_IMPL(hqc_close), _sc, _pra);
      case 2:
      return _skel_method_7(__QAIC_IMPL(hqc_ping), _h, _sc, _pra);
      case 3:
      return _skel_method_6(__QAIC_IMPL(hqc_decode_one), _h, _sc, _pra);
      case 4:
      return _skel_method_5(__QAIC_IMPL(hqc_decode_bench), _h, _sc, _pra);
      case 5:
      return _skel_method_5(__QAIC_IMPL(hqc_decode_bench_count), _h, _sc, _pra);
      case 6:
      return _skel_method_4(__QAIC_IMPL(hqc_substage_bench), _h, _sc, _pra);
      case 7:
      return _skel_method_3(__QAIC_IMPL(hqc_payload_in), _h, _sc, _pra);
      case 8:
      return _skel_method_2(__QAIC_IMPL(hqc_payload_out), _h, _sc, _pra);
      case 9:
      return _skel_method_1(__QAIC_IMPL(hqc_payload_inout), _h, _sc, _pra);
      case 10:
      return _skel_method(__QAIC_IMPL(hqc_decode_buffer_bench), _h, _sc, _pra);
   }
   return AEE_EUNSUPPORTED;
}
#ifdef __cplusplus
}
#endif
#endif //_HQC_SKEL_H
