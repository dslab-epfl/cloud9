// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t2.bc
// RUN: %klee --init-env --posix-runtime --exit-on-error %t2.bc --sym-files 1 10

#include <assert.h>
#include <fcntl.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char **argv) {
  int fd = open("A", O_TRUNC | O_RDWR);
  assert(fd == 3);
  assert(!close(0));
  assert(!close(1));
  assert(close(0) == -1);
  assert(close(1) == -1);
  assert(open("A", O_TRUNC) == 0);  
  assert(dup(0) == 1);
  assert(open("A", O_TRUNC) == 4);
  assert(!close(1));
  assert(open("A", O_TRUNC) == 1);
  assert(dup(0) != 1);
  assert(dup2(0,1) == 1);
  
  return 0;
}
