#include <unistd.h>
#include <stdio.h>
#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <klee/klee.h>

int myglobal_a;
int myglobal_b;

//int a;
//int b;

int main(int argc, char **argv) {
  klee_make_shared(&myglobal_a, sizeof(myglobal_a));
  klee_make_shared(&myglobal_b, sizeof(myglobal_b));

  pid_t pid;

  myglobal_a = 0; 
  int a = 0;

  myglobal_b = 0;
  int b = 0;

  FILE *f = fopen("A", "w+");
  assert(f != NULL && "file does not exist");

  int fd = fileno(f);
  struct stat stat;
  int res = fstat(fd, &stat);

  if (res == -1)
    perror("fstat");

  assert(((stat.st_mode & S_IRUSR) | (stat.st_mode & S_IRGRP) | (stat.st_mode & S_IROTH)));

  fwrite("A", sizeof("A"), 1, f); fflush(f);

  printf("Common area: %d; sa = %d, sb = %d; a = %d, b = %d; f = %lx\n", pid, myglobal_a, myglobal_b, a, b, f);

  pid = fork();

  if (pid > 0) {
    myglobal_a = 10; a = 10;
    printf("I'm in the parent: %d; sa = %d, sb = %d; a = %d, b = %d; f = %lx\n", pid, myglobal_a, myglobal_b, a, b, f);
    myglobal_a = 20; a = 20;

    assert(f != NULL && "Klee bug in the parent");
    fwrite("B", sizeof("B"), 1, f); fflush(f);
    fclose(f);
    f = 0xABABABAB;

    wait(NULL);

    return 1;
  } else if (pid == 0) {
    myglobal_b = 10; b = 10;
    printf("I'm in the child; sa = %d, sb = %d; a = %d, b = %d; f = %lx\n", myglobal_a, myglobal_b, a, b, f);
    myglobal_b = 20; b = 20;

    assert(f != NULL && "Klee bug in the child");
    fwrite("C", sizeof("C"), 1, f); fflush(f);
    fclose(f);
    f = 0xBABABABA;

    return 0;
  } else {
    printf("Something bad happened.\n");
    return 2;
  }
}
