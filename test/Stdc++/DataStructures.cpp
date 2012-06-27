// RUN: %llvmgxx -flto -Wl,-plugin-opt=also-emit-llvm %s -O0 -L../ -lstdc++ -o %t1
// RUN: %klee --no-output --exit-on-error --posix-runtime --libc=uclibc %t1.bc --sym-arg 1 --sym-arg 1

#include <assert.h>
#include <vector>
#include <list>

int main(int argc, char** argv) {
  std::vector<char*> vector;
  vector.push_back(argv[0]);
  vector.push_back(argv[1]);
  assert(vector.at(0) == argv[0]);
  assert(vector.at(1) == argv[1]);
  assert(vector.back() == argv[1]);
  vector.pop_back();
  assert(vector.back() == argv[0]);
  vector.pop_back();
  assert(vector.size() == 0);

  std::list<char*> list;
  list.push_back(argv[0]);
  list.push_front(argv[1]);
  assert(list.back() == argv[0]);
  list.pop_back();
  assert(list.front() == argv[1]);
  list.pop_front();
  assert(list.size() == 0);

  return 0;
}
