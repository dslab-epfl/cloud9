#include <stdio.h>


void myfunc(int *addr) {
  *addr = 0;
}

int main(int argc, char **argv) {
  int myvar;
  myfunc(&myvar);
  return myvar;
}
