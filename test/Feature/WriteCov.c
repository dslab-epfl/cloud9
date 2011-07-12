// RUN: %llvmgcc %s -emit-llvm -g -c -o %t2.bc
// RUN: echo WriteCov.c > ./files-coverable
// RUN: %klee --coverable-modules ./files-coverable --exit-on-error --write-cov %t2.bc
// RUN: grep WriteCov.c:11 klee-last/test000002.cov
// RUN: grep WriteCov.c:13 klee-last/test000001.cov

#include <assert.h>

int main() {
  if (klee_range(0,2, "range")) {
    assert(__LINE__ == 11); printf("__LINE__ = %d\n", __LINE__);
  } else {
    assert(__LINE__ == 13); printf("__LINE__ = %d\n", __LINE__);
  }
  return 0;
}
