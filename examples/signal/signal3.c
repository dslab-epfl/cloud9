#include <unistd.h>
#include <sys/types.h>
#include <pthread.h>
#include <stdio.h>
#include <signal.h>

void signal_handler(int value) {

  printf("Received signal %d in PID %d\n", value, getpid());

}

int main(int argc, char **argv) {
  pid_t pid;
  pthread_t tid;
  struct sigaction sig_act;

  sig_act.sa_flags = 0;
  sig_act.sa_handler = signal_handler;
  sigemptyset(&sig_act.sa_mask);
  sigaction(SIGUSR1, &sig_act, NULL);

  pid = fork();

  if (pid > 0) {
    pid_t child_pid = pid;
    pid = getpid();
    tid = pthread_self();

    printf("I'm in the parent: %d, %d\n", pid, tid);

    printf("Signaling child PID: %d\n", child_pid);

    kill(child_pid, SIGUSR1);

    wait(NULL);
    return 1;
  } else if (pid == 0) {
    pid = getpid();
    tid = pthread_self();

    printf("I'm in the child: %d, %d\n", pid, tid);

    printf("Signaling parent PID: %d\n", getppid());

    kill(getppid(), SIGUSR1);
    kill(getppid(), SIGKILL);

    return 0;
  } else {
    printf("Something bad happened.\n");
    return 2;
  }
}
