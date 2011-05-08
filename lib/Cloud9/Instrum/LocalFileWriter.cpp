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

#include "cloud9/instrum/LocalFileWriter.h"

#include <iostream>
#include <boost/io/ios_state.hpp>


namespace cloud9 {

namespace instrum {

LocalFileWriter::LocalFileWriter(std::ostream &s, std::ostream &e, std::ostream &c) :
	statsStream(s), eventsStream(e), coverageStream(c) {

}

LocalFileWriter::~LocalFileWriter() {

}

void LocalFileWriter::writeStatistics(InstrumentationManager::TimeStamp &time,
		InstrumentationManager::statistics_t &stats) {
	statsStream << time;

	for (unsigned i = 0; i < stats.size(); i++) {
	  if (stats[i] == 0)
	    continue;

	  statsStream << ' ' << i << '=' << stats[i];
	}

	statsStream << endl;
}

void LocalFileWriter::writeEvents(InstrumentationManager::events_t &events) {
	for (InstrumentationManager::events_t::iterator it = events.begin();
			it != events.end(); it++) {
		eventsStream << (*it).first << ' ' << (*it).second.first << ' ' <<
				(*it).second.second << endl;
	}
}

void LocalFileWriter::writeCoverage(InstrumentationManager::TimeStamp &time,
        InstrumentationManager::coverage_t &coverage) {
  coverageStream << time;

  boost::io::ios_all_saver saver(coverageStream);
  coverageStream.precision(2);
  coverageStream << fixed;

  for (InstrumentationManager::coverage_t::iterator it = coverage.begin();
      it != coverage.end(); it++) {
    coverageStream << ' ' << it->first << '=';
    if (it->second.second) {
      coverageStream << it->second.first << '/' << it->second.second;

      double perc = ((double)it->second.first)*100/it->second.second;
      coverageStream << '(' << perc << ')';
    } else {
      coverageStream << it->second.first;
    }


  }

  coverageStream << endl;
}



}

}
