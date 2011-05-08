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

#ifndef LOGGER_H_
#define LOGGER_H_

#include <string>
#include <ostream>
#include <iostream>

#include "llvm/System/TimeValue.h"

#define CLOUD9_DEFAULT_LOG_PREFIX			"Cloud9:\t"
#define CLOUD9_DEFAULT_ERROR_PREFIX			"ERROR:\t"
#define CLOUD9_DEFAULT_INFO_PREFIX		"Info:\t"
#define CLOUD9_DEFAULT_DEBUG_PREFIX			"Debug:\t"

#define CLOUD9_ERROR(msg)	cloud9::Logger::getLogger().getInfoStream() << \
	msg << std::endl

#define CLOUD9_INFO(msg)	cloud9::Logger::getLogger().getInfoStream() << \
	msg << std::endl

#define CLOUD9_DEBUG(msg)	cloud9::Logger::getLogger().getDebugStream() << \
	msg << std::endl

#define CLOUD9_EXIT(msg) do { CLOUD9_ERROR(msg); exit(1); } while(0)

#define CLOUD9_STACKTRACE cloud9::Logger::getStackTrace()



namespace cloud9 {

class Logger {
private:
	static Logger logger;

	std::string logPrefix;
	std::string errorPrefix;
	std::string infoPrefix;
	std::string debugPrefix;

	std::string timeStamp;

	llvm::sys::TimeValue startTime;
protected:
	Logger() :
		logPrefix(CLOUD9_DEFAULT_LOG_PREFIX),
		errorPrefix(CLOUD9_DEFAULT_ERROR_PREFIX),
		infoPrefix(CLOUD9_DEFAULT_INFO_PREFIX),
		debugPrefix(CLOUD9_DEFAULT_DEBUG_PREFIX),
		startTime(llvm::sys::TimeValue::now()){

	}

	virtual ~Logger() {

	}

	void updateTimeStamp();

public:
	static Logger &getLogger();

	const std::string &getLogPrefix() { return logPrefix; }
	void setLogPrefix(std::string prefix) { logPrefix = prefix; }

	std::ostream &getInfoStream() {
		updateTimeStamp();
		return getInfoStreamRaw() << timeStamp << logPrefix << infoPrefix;
	}

	std::ostream &getErrorStream() {
		updateTimeStamp();
		return getErrorStreamRaw() << timeStamp << logPrefix << errorPrefix;
	}

	std::ostream &getDebugStream() {
		updateTimeStamp();
		return getDebugStreamRaw() << timeStamp << logPrefix << debugPrefix;
	}

	static std::string getStackTrace();

	virtual std::ostream &getInfoStreamRaw() { return std::cerr; };
	virtual std::ostream &getErrorStreamRaw() { return std::cerr; };
	virtual std::ostream &getDebugStreamRaw() { return std::cerr; };
};

}

std::string operator*(const std::string input, unsigned int n);

#endif /* LOGGER_H_ */
