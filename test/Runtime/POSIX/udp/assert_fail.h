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


#ifndef ASSERT_FAIL_H_
#define ASSERT_FAIL_H_

#ifdef _IS_LLVM
#include <klee/klee.h>
#endif
#include <errno.h>  //errno
#include <string.h> //strerror
#include <stdlib.h> //exit
#ifndef _IS_LLVM
#include <stdio.h> //printf
#endif


#ifdef _IS_LLVM
static void __print(const char* message)
{
  klee_debug(message);
}
#else
static void __print(const char* message)
{
  printf("%s", message);
}
#endif


void assert_fail(const char* message)
{
  __print("\n--------------- TEST FAILED ---------------\n");
  __print(message);
  __print("\nerrno: ");
  const char* errno_as_string = strerror(errno);
  __print(errno_as_string);
  __print("\n\n");

#ifdef _IS_LLVM
  klee_report_error("", 0, message, "test_fail");
#else
  exit(-1);  
#endif
}

//TODO: ah: remove copy-paste: call my_assert
#define assert_return(func) if((func) == -1) assert_fail(__STRING(func_error_returned\x3A\t##func));

#define my_assert(expr) if((expr) == 0) assert_fail(__STRING(expr_is_false\x3A\t##expr));

#endif /* ASSERT_FAIL_H_ */
