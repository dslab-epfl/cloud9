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

#include "cloud9/worker/KleeCommon.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "llvm/Support/Path.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"

#include <string>
#include <cstdlib>

#include <glog/logging.h>

using llvm::sys::Path;
using llvm::SmallString;
using llvm::Twine;

using namespace llvm;

std::string getKleePath() {
  // First look for $KLEE_ROOT, then KLEE_DIR
  const char *kleePathName = std::getenv(KLEE_ROOT_VAR);
  if (!kleePathName)
    kleePathName = KLEE_DIR;

  SmallString<256> newPath(kleePathName);

  
  sys::fs::make_absolute(newPath);
  LOG(INFO) << "Using Klee path " << std::string(newPath.str());
  return newPath.str();
}

std::string getKleeLibraryPath() {
  std::string kleePathName = getKleePath();

  Path libraryPath(kleePathName);
  libraryPath.appendComponent(RUNTIME_CONFIGURATION);
  libraryPath.appendComponent("lib");

  return libraryPath.str();
}

std::string getUclibcPath() {
  const char *uclibcPathName = std::getenv(KLEE_UCLIBC_ROOT_VAR);
  if (!uclibcPathName)
    uclibcPathName = KLEE_UCLIBC;

  SmallString<256> newPath(uclibcPathName);

  sys::fs::make_absolute(newPath);
  LOG(INFO) << "Using uClibc path " << std::string(newPath.str());
  return newPath.str();
}
