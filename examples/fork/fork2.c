#include <unistd.h>
#include <stdio.h>

#include <klee/klee.h>

int myglobal_a;
int myglobal_b;

int main(int argc, char **argv) {
  klee_make_shared(&myglobal_a, sizeof(myglobal_a));
  klee_make_shared(&myglobal_b, sizeof(myglobal_b));

  pid_t pid;
  printf("I'm in the common area.\n");

  myglobal_a = 0;
  myglobal_b = 0;

  pid = fork();

  if (pid > 0) {
    myglobal_a = 10;
    printf("I'm in the parent: %d; a = %d, b = %d\n", pid, myglobal_a, myglobal_b);
    return 1;
  } else if (pid == 0) {
    myglobal_b = 10;
    printf("I'm in the child; a = %d, b = %d\n", myglobal_a, myglobal_b);
    return 0;
  } else {
    printf("Something bad happened.\n");
    return 2;
  }
}
