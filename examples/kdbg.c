#include <stdio.h>
#include <klee/klee.h>

int main(int argc, char **argv) {
  char *msg = "simple debug message: %s\n";
  char *sparam = "parameter";
  int iparam = 42;

  klee_debug(msg, sparam);
  klee_debug("debug sparam: %s\n", sparam);
  klee_debug("debug iparam: %d\n", iparam);

  klee_make_symbolic(&msg, sizeof(msg), "msg");
  klee_make_symbolic(&sparam, sizeof(sparam), "sparam");
  klee_make_symbolic(&iparam, sizeof(iparam), "iparam");

  klee_debug(msg, sparam);
  klee_debug("debug sparam: %s\n", sparam);
  klee_debug("debug iparam: %d\n", iparam);
  return 0;
}
