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

#ifndef UDP_TESTS_H_
#define UDP_TESTS_H_


#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>  //printf
#include <sys/types.h>
#include <sys/socket.h>
#include <assert.h>
#include <string.h> //memset

#ifdef _IS_LLVM
#include "sockets.h" //__net, __unix_net
#include "config.h"  //MAX_PORTS, MAX_UNIX_EPOINTS
#endif

#define SRV_IP "127.0.0.1"

void setupAddress(struct sockaddr_in *addr, int port)
{
  memset((char*)addr, 0, sizeof (*addr));
  addr->sin_family = AF_INET;
  addr->sin_port = htons(port);
  assert(inet_aton(SRV_IP, &addr->sin_addr) != 0);
}

#ifdef _IS_LLVM
static int __get_number_of_allocated_endpoints(end_point_t* end_points, size_t size) {

  int i = 0;
  int number_of_allocated_endpoints = 0;

  for (; i < size; ++i) {
    if(end_points[i].allocated == 1)
      ++number_of_allocated_endpoints;
  }

  return number_of_allocated_endpoints;
}
#endif

int get_number_of_allocated_net_endpoints() {
#ifdef _IS_LLVM
  return __get_number_of_allocated_endpoints(__net.end_points, MAX_PORTS);
#else
  printf("warning: __get_number_of_allocated_endpoints: if compile with gcc -> always returns 0\n");
  return 0;
#endif
}

int get_number_of_allocated_unix_endpoints() {
#ifdef _IS_LLVM
  return __get_number_of_allocated_endpoints(__unix_net.end_points, MAX_UNIX_EPOINTS);
#else
  printf("warning: __get_number_of_allocated_endpoints: if compile with gcc -> always returns 0\n");
  return 0;
#endif
}


#endif /* UDP_TESTS_H_ */
