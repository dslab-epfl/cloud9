// RUN: %llvmgcc -I../../../../../runtime/POSIX %s -emit-llvm -c -o %t.bc
// RUN: %klee  --exit-on-error --posix-runtime --libc=uclibc --no-output --simplify-sym-indices --output-module --disable-inlining --allow-external-sym-calls %t.bc

/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright (c) 2011, Dependable Systems Laboratory, EPFL
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Dependable Systems Laboratory, EPFL nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE DEPENDABLE SYSTEMS LABORATORY, EPFL BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * All contributors are listed in CLOUD9-AUTHORS file.
 *
*/

#include "udp_test_helpers.h"
#include "assert_fail.h"
#include <assert.h>

#define BUFLEN 512


int main(int argc, char **argv) {
  int socketFd;
  assert_return(socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));

  struct sockaddr_in addr_me;
  setupAddress(&addr_me, 7779);
  assert_return(bind(socketFd, (struct sockaddr*) &addr_me, sizeof(addr_me)));
  assert_return(connect(socketFd, (struct sockaddr*) &addr_me, sizeof addr_me));

  char buf[BUFLEN];
  assert (send(socketFd, buf, BUFLEN, 0) == BUFLEN);
  assert (recv(socketFd, buf, BUFLEN, 0) == BUFLEN); //todo: ah: use MSG_DONTWAIT

  assert_return (close(socketFd) );

  assert(get_number_of_allocated_net_endpoints() == 0);
}
