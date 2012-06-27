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

#ifndef PROTOCOLS_H_
#define PROTOCOLS_H_

#include "cloud9/Cloud9Data.pb.h"

#include "cloud9/ExecutionPath.h"

#include <string>
#include <cassert>
#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/function.hpp>
#include <boost/lexical_cast.hpp>
#include <cstring>


#define CLOUD9_STAT_NAME_LOCAL_COVERAGE "LIcov"
#define CLOUD9_STAT_NAME_GLOBAL_COVERAGE "GIcov"

using namespace boost::asio::ip;
using namespace cloud9::data;

namespace cloud9 {


class ExecutionPath;
class ExecutionPathSet;

typedef std::vector<std::pair<uint32_t, uint64_t> > cov_update_t;

void embedMessageLength(std::string &message);

void sendMessage(tcp::socket &socket, std::string &message);
void recvMessage(tcp::socket &socket, std::string &message);


class AsyncMessageReader {
public:
  typedef boost::function<void (std::string&,
        const boost::system::error_code&)> Handler;
private:
  tcp::socket &socket;

  char *msgData;
  size_t msgSize;

  void reset() {
    if (msgData) {
      delete[] msgData;
      msgData = NULL;
      msgSize = 0;
    }
  }

  void handleHeaderRead(const boost::system::error_code &error, size_t size, Handler handler) {
    if (!error) {
      assert(size == sizeof(msgSize));

      msgData = new char[msgSize];

      boost::asio::async_read(socket, boost::asio::buffer(msgData, msgSize),
          boost::bind(&AsyncMessageReader::handleMessageRead,
              this, boost::asio::placeholders::error,
              boost::asio::placeholders::bytes_transferred,
              handler));
    } else {
      std::string message;

      handler(message, error);
      reset();
    }
  }

  void handleMessageRead(const boost::system::error_code &error, size_t size, Handler handler) {
    std::string message;
    if (!error) {
      message = std::string(msgData, msgSize);
    }

    handler(message, error);
    reset();
  }
public:
  AsyncMessageReader(tcp::socket &s) :
    socket(s), msgData(NULL), msgSize(0) { }

  virtual ~AsyncMessageReader() {}

  void recvMessage(Handler handler) {
    assert(msgData == NULL);

    boost::asio::async_read(socket, boost::asio::buffer(&msgSize, sizeof(msgSize)),
        boost::bind(&AsyncMessageReader::handleHeaderRead,
            this, boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred,
            handler));
  }
};

class AsyncMessageWriter {
public:
  typedef boost::function<void (const boost::system::error_code&)> Handler;
private:
  tcp::socket &socket;

  std::string message;

  void handleMessageWrite(const boost::system::error_code &error, size_t size, Handler handler) {
    handler(error);
  }

public:
  AsyncMessageWriter(tcp::socket &s) :
    socket(s) { }

  virtual ~AsyncMessageWriter() { }

  void sendMessage(const std::string &message, Handler handler) {
    size_t msgSize = message.size();
    this->message.clear();
    this->message.append((char*)&msgSize, sizeof(msgSize));
    this->message.append(message.begin(), message.end());

    boost::asio::async_write(socket, boost::asio::buffer(this->message),
        boost::bind(&AsyncMessageWriter::handleMessageWrite,
            this, boost::asio::placeholders::error,
            boost::asio::placeholders::bytes_transferred,
            handler));
  }
};


ExecutionPathSetPin parseExecutionPathSet(const cloud9::data::ExecutionPathSet &ps);

void serializeExecutionPathSet(ExecutionPathSetPin &set,
      cloud9::data::ExecutionPathSet &result);

void parseStatisticUpdate(const cloud9::data::StatisticUpdate &update,
    cov_update_t &data);

void serializeStatisticUpdate(const std::string &name, const cov_update_t &data,
    cloud9::data::StatisticUpdate &update);
 
void connectSocket(boost::asio::io_service &service, tcp::socket &socket,
    std::string &address, int port, boost::system::error_code &error);

/* Functions useful for debugging */

std::string covUpdatesToString(cov_update_t &covUpdate);

static inline std::string getASCIIMessage(std::string &message) {
    std::string result;
    bool first = true;

    for (std::string::iterator it = message.begin(); it != message.end(); it++) {
        if (!first)
            result.push_back(':');
        else
            first = false;

        result.append(boost::lexical_cast<std::string>((int)(*it)));
    }
    return result;
}

}


#endif /* PROTOCOLS_H_ */
