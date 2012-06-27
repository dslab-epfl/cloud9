// RUN: %llvmgxx -flto -Wl,-plugin-opt=also-emit-llvm %s -O0 -L../ -lstdc++ -o %t1
// RUN: %klee --no-output --exit-on-error --posix-runtime --libc=uclibc %t1.bc --sym-arg 1 --sym-arg 1

#include <iostream>

int main(int argc, char** argv) {
  std::cout<<argv[1]<<std::endl;
  std::cout.flush();
  std::cout.put(argv[2][0]);
}
