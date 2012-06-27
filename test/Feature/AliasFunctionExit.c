// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t1.bc
// RUN: %klee --posix-runtime %t1.bc --no-overlapped > %t1.log
// RUN: grep -c START %t1.log | grep 1
// RUN: grep -c END %t1.log | grep 2

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void start(int x) {
  printf("START\n");
  fflush(stdout); //otherwise you can get 'START' printed twice
  if (x == 53) 
    exit(1);
}

void end(int status) {
  klee_alias_function("exit", "exit");
  printf("END: status = %d\n", status);
	fflush(stdout);
  exit(status);
}


int main(int argc, char** argv) {
  int x;
  klee_make_symbolic(&x, sizeof(x), "x");

  klee_alias_function("exit", "end");
  
  start(x);
  end(0);
}
