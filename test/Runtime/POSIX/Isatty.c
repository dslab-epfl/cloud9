// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --libc=uclibc --posix-runtime --init-env %t.bc --sym-files 0 10 --no-overlapped >%t.log

// RUN: test -f klee-last/test000001.ktest

// RUN: grep -q "stdin is NOT a tty" %t.log
// RUN: grep -q "stdout is NOT a tty" %t.log

#include <unistd.h>
#include <stdio.h>
#include <assert.h>

int main(int argc, char** argv) {
  int fd0 = 0; // stdin
  int fd1 = 1; // stdout

  int r = isatty(fd0);
  if (r) 
    printf("stdin is a tty\n");
  else printf("stdin is NOT a tty\n");
  
  r = isatty(fd1);
  if (r) 
    printf("stdout is a tty\n");
  else printf("stdout is NOT a tty\n");

  return 0;
}
