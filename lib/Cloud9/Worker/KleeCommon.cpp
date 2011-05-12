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

#include "cloud9/Logger.h"
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 9)
#include "llvm/System/Path.h"
#else
#include "llvm/Support/Path.h"
#endif

#include <string>
#include <cstdlib>

using llvm::sys::Path;
using llvm::Twine;

#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR < 7)

std::string getKleePath() {
	// First look for $KLEE_ROOT, then KLEE_DIR
	char *kleePathName = std::getenv(KLEE_ROOT_VAR);
	Path kleePath;

	if (kleePathName != NULL) {
		// Check whether the path exists
		kleePath = Path(kleePathName);
		CLOUD9_DEBUG("Found KLEE_ROOT variable " << kleePath.toString());

		if (kleePath.isValid()) {
			// The path exists, so we return it
			kleePath.makeAbsolute();
			CLOUD9_DEBUG("Using Klee path " << kleePath.toString());
			return kleePath.toString();
		} else {
			CLOUD9_DEBUG("Cannot use KLEE_ROOT variable. Path is not valid.");
		}
	}

	kleePath = Path(KLEE_DIR);
	kleePath.makeAbsolute();
	CLOUD9_DEBUG("Using Klee path " << kleePath.toString());
	return kleePath.toString();
}

std::string getKleeLibraryPath() {
	std::string kleePathName = getKleePath();

	Path libraryPath(kleePathName);
	libraryPath.appendComponent(RUNTIME_CONFIGURATION);
	libraryPath.appendComponent("lib");

	return libraryPath.toString();
}

std::string getUclibcPath() {
	char *uclibcPathName = std::getenv(KLEE_UCLIBC_ROOT_VAR);
	Path uclibcPath;

	if (uclibcPathName != NULL) {
		uclibcPath = Path(uclibcPathName);

		if (uclibcPath.isValid()) {
			uclibcPath.makeAbsolute();
			return uclibcPath.toString();
		}
	}

	uclibcPath = Path(KLEE_UCLIBC);
	uclibcPath.makeAbsolute();

	return uclibcPath.toString();
}

#else

std::string getKleePath() {
	// First look for $KLEE_ROOT, then KLEE_DIR
	char *kleePathName = std::getenv(KLEE_ROOT_VAR);
  Path kleePath;

	if (kleePathName != NULL) {
		CLOUD9_DEBUG("Found KLEE_ROOT variable " << kleePathName);

    if(llvm::sys::path::is_absolute(Twine(kleePathName)))
      kleePath = Path(kleePathName);
    else 
      kleePath = llvm::sys::Path::GetCurrentDirectory();

		// Check whether the path exists
    kleePath.appendSuffix(kleePathName);
		if (kleePath.isValid()) {
			CLOUD9_DEBUG("Using Klee path " << kleePath.str());
			return kleePath.str();
		} else {
			CLOUD9_DEBUG("Cannot use KLEE_ROOT variable. Path is not valid.");
		}
	}

  if(KLEE_DIR !=NULL && llvm::sys::path::is_absolute(Twine(KLEE_DIR)))
    kleePath = Path(KLEE_DIR);
  else{ 
    kleePath = llvm::sys::Path::GetCurrentDirectory();
    kleePath.appendSuffix(KLEE_DIR);
  }
	CLOUD9_DEBUG("Using Klee path " << kleePath.str());
	return kleePath.str();
}

std::string getKleeLibraryPath() {
	std::string kleePathName = getKleePath();

	Path libraryPath(kleePathName);
	libraryPath.appendComponent(RUNTIME_CONFIGURATION);
	libraryPath.appendComponent("lib");

	return libraryPath.str();
}

std::string getUclibcPath() {
	// First look for $KLEE_UCLIBC_ROOT, then KLEE_UCLIBC_DIR
	char *uclibcPathName = std::getenv(KLEE_UCLIBC_ROOT_VAR);
  Path uclibcPath;

	if (uclibcPathName != NULL) {
		CLOUD9_DEBUG("Found KLEE_UCLIBC_ROOT variable " << uclibcPathName);

    if(llvm::sys::path::is_absolute(Twine(uclibcPathName)))
      uclibcPath = Path(uclibcPathName);
    else 
      uclibcPath = llvm::sys::Path::GetCurrentDirectory();

		// Check whether the path exists
    uclibcPath.appendSuffix(uclibcPathName);
		if (uclibcPath.isValid()) {
			CLOUD9_DEBUG("Using Klee path " << uclibcPath.str());
			return uclibcPath.str();
		} else {
			CLOUD9_DEBUG("Cannot use KLEE_UCLIBC_ROOT variable. Path is not valid.");
		}
	}

  if(KLEE_UCLIBC !=NULL && llvm::sys::path::is_absolute(Twine(KLEE_UCLIBC)))
    uclibcPath = Path(KLEE_UCLIBC);
  else{ 
    uclibcPath = llvm::sys::Path::GetCurrentDirectory();
    uclibcPath.appendSuffix(KLEE_UCLIBC);
  }
	CLOUD9_DEBUG("Using Uclibc path " << uclibcPath.str());
	return uclibcPath.str();
}

#endif
