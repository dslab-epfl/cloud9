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

#define _IS_LLVM

#include "udp_test_helpers.h"
#include "assert_fail.h"

#define BUF_LENGTH 1024

int main(int argc, char **argv) {
  int sender_port = 6666, recv_port = 7777;

  struct sockaddr_in recv_addr, sender_addr;
  setupAddress(&recv_addr, recv_port);
  setupAddress(&sender_addr, sender_port);

  int senderFd;
  assert_return(senderFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  assert_return(bind(senderFd, (const struct sockaddr*)&sender_addr, sizeof sender_addr));

  int recvFd;
  assert_return(recvFd = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, IPPROTO_UDP));
  assert_return(bind(recvFd, (const struct sockaddr*)&recv_addr, sizeof recv_addr));

  //sendto
  char buf[BUF_LENGTH];
  int datagram_size;
  assert_return(datagram_size = sprintf(buf, "This is packet %d\n", 666));
  my_assert(sendto(senderFd, buf, datagram_size, 0,
      (const struct sockaddr*)&recv_addr, sizeof(struct sockaddr_in)) == datagram_size);

  //recvfrom
  socklen_t addr_len = 10000000;
  struct sockaddr_in actual_sender_addr;
  int res = recvfrom(recvFd, buf, BUF_LENGTH, 0, (struct sockaddr*)&actual_sender_addr, &addr_len);
  my_assert(res == datagram_size);

  assert(addr_len == sizeof sender_addr);
  assert(actual_sender_addr.sin_port == sender_addr.sin_port);
  assert(actual_sender_addr.sin_family == sender_addr.sin_family);

  assert_return(close(recvFd));
  assert_return(close(senderFd));

  assert(get_number_of_allocated_net_endpoints() == 0);

  return 0;
}

#undef _IS_LLVM
