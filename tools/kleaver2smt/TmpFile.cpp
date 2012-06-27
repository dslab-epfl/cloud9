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


#include "TmpFile.h"

#include <stdlib.h>
#include <unistd.h>
#include <string>
#include <sstream>
#include <stdio.h>
#include <cstring>

#include "assert_fail.h"


using namespace std;

TmpFile::TmpFile() {
  char pattern[] = "tmpXXXXXX";
  _fd = mkstemp(pattern);
  my_assert(_fd != -1);

  stringstream ss;
  ss << "/proc/self/fd/" << _fd;
  char fileNameChars[1024];
  memset(fileNameChars, 0, sizeof fileNameChars);

  int nameIsKnown = readlink(ss.str().c_str(), fileNameChars, sizeof fileNameChars);
  my_assert(nameIsKnown);

  FileName = string(fileNameChars);
}

const string& TmpFile::getTmpFileName() const {
  return FileName;
}

TmpFile::~TmpFile() {
  int closed = close(_fd);
  my_assert(closed == 0);
  int isRemoved = remove(FileName.c_str());
  my_assert(isRemoved == 0);
}

void TmpFile::write(const string& str) {
  ssize_t res = ::write(_fd, (const void*)str.c_str(), str.size());
  my_assert(res == (int)str.size());
}


