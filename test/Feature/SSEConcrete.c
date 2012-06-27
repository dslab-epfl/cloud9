// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --no-output --exit-on-error %t1.bc

#include "xmmintrin.h"
#include "emmintrin.h"
#include "assert.h"
#include "klee/klee.h"

void zlimit (int simd, float *src , float *dst , size_t size) {
  if (simd) {
    __m128 zero4 = _mm_set1_ps(0.f);
    while ( size >= 4) {
      __m128 srcv = _mm_loadu_ps(src);
      __m128 cmpv = _mm_cmpgt_ps(srcv, zero4);
      __m128 dstv = _mm_and_ps(cmpv, srcv);
      _mm_storeu_ps(dst, dstv);
      src += 4; dst += 4; size -= 4;
    }
  }
  else {
    while (size) {
      *dst = *src > 0.f ? *src : 0.f ;
      src++; dst++; size--;
    }
  }
}

void avg (int simd, uint8_t *src , uint8_t *src2, uint8_t *dst) {
  if (simd) {
    (*(__m128i*)dst) = _mm_avg_epu8((*(__m128i*)src), (*(__m128i*)src2));
  }
  else {
    int size = 16;
    while (size) {
      *dst = ((*src)+(*src2))/2;
      src++; src2++; dst++; size--;
    }
  }
}

int main(void) {
  float src[64], dstv[64], dsts[64];
  int i;
  for(i=0; i<64; ++i)
    src[i] = i-32;
  zlimit (0, src, dsts, 64);
  zlimit (1, src, dstv, 64);
  for (i = 0; i < 64; ++i)
    assert(dstv[i] == dsts[i]);

  uint8_t srci_a[16], srci_b[16], dstvi[16], dstsi[16];
  for(i=0; i<16; ++i)  {
    srci_a[i] = i;
    srci_b[i] = i+10;
  }
  avg (0, srci_a, srci_b, dstsi);
  avg (1, srci_a, srci_b, dstvi);
  for(i=0; i<16; ++i)
    assert(dstvi[i] == dstsi[i]);
}

