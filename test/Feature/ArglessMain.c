// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --posix-runtime --libc=uclibc --exit-on-error %t1.bc

#include <stdio.h>

int main() {
  printf("Main executed");
  return 0;
}
