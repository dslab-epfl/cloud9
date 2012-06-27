#include <stdio.h>
#include "klee/klee.h"

int main(int argc, char **argv) {
  unsigned int limit;
  klee_make_symbolic(&limit, sizeof(unsigned int), "limit");
  int i;

  for (i = 0; i < limit; i++) {
    printf("iteration %d ", i);
  }
  printf("done!\n");
  return 0;
}
