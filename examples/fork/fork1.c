#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdio.h>

int main(int argc, char **argv) {
  pid_t pid;
  pthread_t tid;

  printf("I'm in the common area.\n");

  pid = fork();

  if (pid > 0) {
    pid = getpid();
    tid = pthread_self();

    printf("I'm in the parent: %d, %d\n", pid, tid);

    wait(NULL);
    return 1;
  } else if (pid == 0) {
    pid = getpid();
    tid = pthread_self();

    printf("I'm in the child: %d, %d\n", pid, tid);
    return 0;
  } else {
    printf("Something bad happened.\n");
    return 2;
  }
}
