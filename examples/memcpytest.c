#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int main(int argc, char **argv) {
  char buff[256];

  memset(buff, 0, 256);
  buff[0] = 'a';
  printf("%s\n", buff);
  return 0;
}
