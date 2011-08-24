// RUN: %llvmgcc %s -emit-llvm -O0 -c -o %t.bc
// RUN: %klee --init-env --posix-runtime --exit-on-error %t.bc --sym-files 0 4

#include <sys/types.h>
#include <unistd.h>

#ifdef __USE_MISC
#undef __USE_MISC
#define __REDEF_MISC
#endif

#ifdef __USE_XOPEN2K8
#undef __USE_XOPEN2K8
#define __REDEF_XOPEN2K8
#endif

#include <sys/stat.h>

#ifdef __REDEF_MISC
#define __USE_MISC 1
#undef __REDEF_MISC
#endif

#ifdef __REDEF_XOPEN2K8
#define __USE_XOPEN2K8 1
#undef __REDEF_XOPEN2K8
#endif

#include <assert.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <asm/ioctls.h>
#include <errno.h>
#include <stdio.h>

int main(int argc, char **argv) {
  struct stat s;
  struct termios ts;

  assert(fstat(0, &s) == 0);

  assert(ioctl(10, FIONREAD, &ts) == -1 && errno == EBADF);

  if (S_ISCHR(s.st_mode)) {
    printf("is chr\n");
    assert(ioctl(0, TIOCGWINSZ, &ts) == -1); // No terminal support whatsoever
  } else {
    printf("not chr\n");
    // I think TC* ioctls basically always fail on nonchr?
    assert(ioctl(0, TIOCGWINSZ, &ts) == -1);
    assert(errno == ENOTTY);
  }

  return 0;
}
