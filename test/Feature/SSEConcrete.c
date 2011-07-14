// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --no-output --exit-on-error %t1.bc

#include "xmmintrin.h"
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

int main(void) {
	float src[64], dstv[64], dsts[64];
	uint32_t *dstvi = (uint32_t*) dstv;
	uint32_t *dstsi = (uint32_t*) dsts;
	int i;
	for(i=0; i<64; ++i)
		src[i] = i-32;
	zlimit (0, src, dsts, 64);
	zlimit (1, src, dstv, 64);
	for (i = 0; i < 64; ++i)
		assert(dstvi[i] == dstsi[i]);
}

