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


static size_t get_total_len(struct msghdr* msg) {
  size_t total_bytes = 0;
  int i = 0;
  for(; i< msg->msg_iovlen; ++i) {
    total_bytes += msg->msg_iov[i].iov_len;
  }
  return total_bytes;
}

int main(int argc, char **argv) {
  int sender_port = 6666, rcv_port = 7777;

  struct sockaddr_in recv_addr, sender_addr;
  setupAddress(&recv_addr, rcv_port);
  setupAddress(&sender_addr, sender_port);

  int senderFd;
  assert_return(senderFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  assert_return(bind(senderFd, (const struct sockaddr*)&sender_addr, sizeof sender_addr));
  assert_return(connect(senderFd, (struct sockaddr*)&recv_addr, sizeof recv_addr));

  int recvFd;
  assert_return(recvFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
  assert_return(bind(recvFd, (const struct sockaddr*)&recv_addr, sizeof recv_addr));
  assert_return(connect(recvFd, (struct sockaddr*)&sender_addr, sizeof sender_addr));

  char buf[BUF_LENGTH];
  int datagram_size;
  assert_return(datagram_size = sprintf(buf, "This is packet %d\n", 666));

  struct msghdr msg_hdr;
  struct iovec iovec1, iovec2;
  char tmp1[] = "first iovec";
  iovec1.iov_base = tmp1;
  iovec1.iov_len = sizeof tmp1;
  char tmp2[] = "second iovec";
  iovec2.iov_base = tmp2;
  iovec2.iov_len = sizeof tmp2;

  msg_hdr.msg_iov = (struct iovec*)malloc(sizeof(struct iovec) * 2);
  msg_hdr.msg_iovlen = 2;
  msg_hdr.msg_iov[0] = iovec1;
  msg_hdr.msg_iov[1] = iovec2;

  msg_hdr.msg_flags = 0;
  msg_hdr.msg_name = NULL;
  msg_hdr.msg_controllen = 0;
  msg_hdr.msg_namelen = 0;
  msg_hdr.msg_control = NULL;

  size_t total_bytes = get_total_len(&msg_hdr);
  my_assert(sendmsg(senderFd, &msg_hdr, 0) == total_bytes);

  //truncate message
  char tmp3[1];
  iovec2.iov_base = tmp3;
  iovec2.iov_len = sizeof tmp3;
  msg_hdr.msg_iov[1] = iovec2;
  total_bytes = get_total_len(&msg_hdr);

  my_assert(recvmsg(recvFd, &msg_hdr, 0) == total_bytes);

  assert_return(close(senderFd));
  assert_return(close(recvFd));

  assert(get_number_of_allocated_net_endpoints() == 0);
}

#undef _IS_LLVM
