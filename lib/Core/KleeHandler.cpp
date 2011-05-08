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

#include "klee/KleeHandler.h"

#include <iostream>
#include <fstream>
#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

// FIXME
#include "Common.h"
#include "cloud9/worker/WorkerCommon.h"


#include "klee/Config/config.h"
#include "klee/Internal/System/Time.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/ExecutionState.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/System/Path.h"

#include "cloud9/instrum/InstrumentationManager.h"
#include "cloud9/instrum/LocalFileWriter.h"

using namespace llvm;

#define CLOUD9_STATS_FILE_NAME      "c9-stats.txt"
#define CLOUD9_EVENTS_FILE_NAME     "c9-events.txt"
#define CLOUD9_COVERAGE_FILE_NAME   "c9-coverage.txt"

namespace {

cl::opt<std::string>
		OutputDir("output-dir", cl::desc(
				"Directory to write results in (defaults to klee-out-N)"),
				cl::init(""));

cl::opt<bool> WritePaths("write-paths", cl::desc(
		"Write .path files for each test case"));

cl::opt<bool> WriteSymPaths("write-sym-paths", cl::desc(
		"Write .sym.path files for each test case"));

cl::opt<bool> ExitOnError("exit-on-error", cl::desc("Exit if errors occur"));

cl::opt<bool> NoOutput("no-output", cl::desc("Don't generate test files"));

cl::opt<bool> WriteCVCs("write-cvcs", cl::desc(
		"Write .cvc files for each test case"));

cl::opt<bool> WritePCs("write-pcs", cl::desc(
		"Write .pc files for each test case"));

cl::opt<bool> WriteCov("write-cov", cl::desc(
		"Write coverage information for each test case"));

cl::opt<bool> WriteTestInfo("write-test-info", cl::desc(
		"Write additional test case information"));

cl::opt<unsigned>
StopAfterNTests("stop-after-n-tests",
	     cl::desc("Stop execution after generating the given number of tests.  Extra tests corresponding to partially explored paths will also be dumped."),
	     cl::init(0));
}

namespace klee {

KleeHandler::KleeHandler(int argc, char **argv) :
	m_interpreter(0), m_pathWriter(0), m_symPathWriter(0), m_infoFile(0),
			m_testIndex(0), m_pathsExplored(0), m_argc(argc), m_argv(argv) {
	std::string theDir;

	if (OutputDir == "") {
		llvm::sys::Path directory(InputFile);
		std::string dirname = "";
		directory.eraseComponent();

		if (directory.isEmpty())
			directory.set(".");

		for (int i = 0;; i++) {
			char buf[256], tmp[64];
			sprintf(tmp, "klee-out-%d", i);
			dirname = tmp;
			sprintf(buf, "%s/%s", directory.c_str(), tmp);
			theDir = buf;

			if (DIR *dir = opendir(theDir.c_str())) {
				closedir(dir);
			} else {
				break;
			}
		}

		std::cerr << "KLEE: output directory = \"" << dirname << "\"\n";

		llvm::sys::Path klee_last(directory);
		klee_last.appendComponent("klee-last");

		if ((unlink(klee_last.c_str()) < 0) && (errno != ENOENT)) {
			perror("Cannot unlink klee-last");
			assert(0 && "exiting.");
		}

		if (symlink(dirname.c_str(), klee_last.c_str()) < 0) {
			perror("Cannot make symlink");
			assert(0 && "exiting.");
		}
	} else {
		theDir = OutputDir;
	}

	sys::Path p(theDir);
	if (!p.isAbsolute()) {
		sys::Path cwd = sys::Path::GetCurrentDirectory();
		cwd.appendComponent(theDir);
		p = cwd;
	}
	strcpy(m_outputDirectory, p.c_str());

	if (mkdir(m_outputDirectory, 0775) < 0) {
		std::cerr << "KLEE: ERROR: Unable to make output directory: \""
				<< m_outputDirectory << "\", refusing to overwrite.\n";
		exit(1);
	}

	char fname[1024];
	snprintf(fname, sizeof(fname), "%s/warnings.txt", m_outputDirectory);
	klee_warning_file = fopen(fname, "w");
	assert(klee_warning_file);

	snprintf(fname, sizeof(fname), "%s/messages.txt", m_outputDirectory);
	klee_message_file = fopen(fname, "w");
	assert(klee_message_file);

	m_infoFile = openOutputFile("info");

	std::ostream *pidFile = openOutputFile("pid");
	assert(pidFile != NULL);
	*pidFile << getpid();
	delete pidFile;

    // Init Cloud9 instrumentation
    initCloud9Instrumentation();
}

KleeHandler::~KleeHandler() {
	if (m_pathWriter)
		delete m_pathWriter;
	if (m_symPathWriter)
		delete m_symPathWriter;
	delete m_infoFile;
}

void KleeHandler::initCloud9Instrumentation() {
    std::string statsFileName = getOutputFilename(CLOUD9_STATS_FILE_NAME);
    std::string eventsFileName = getOutputFilename(CLOUD9_EVENTS_FILE_NAME);
    std::string coverageFileName = getOutputFilename(CLOUD9_COVERAGE_FILE_NAME);

    std::ostream *instrStatsStream = new std::ofstream(statsFileName.c_str());
    std::ostream *instrEventsStream = new std::ofstream(eventsFileName.c_str());
    std::ostream *instrCovStream = new std::ofstream(coverageFileName.c_str());

    cloud9::instrum::InstrumentationWriter *writer =
            new cloud9::instrum::LocalFileWriter(*instrStatsStream,
                    *instrEventsStream, *instrCovStream);

    cloud9::instrum::theInstrManager.registerWriter(writer);
    cloud9::instrum::theInstrManager.start();
}

void KleeHandler::setInterpreter(Interpreter *i) {
	m_interpreter = i;

	if (WritePaths) {
		m_pathWriter = new TreeStreamWriter(getOutputFilename("paths.ts"));
		assert(m_pathWriter->good());
		m_interpreter->setPathWriter(m_pathWriter);
	}

	if (WriteSymPaths) {
		m_symPathWriter
				= new TreeStreamWriter(getOutputFilename("symPaths.ts"));
		assert(m_symPathWriter->good());
		m_interpreter->setSymbolicPathWriter(m_symPathWriter);
	}
}

std::string KleeHandler::getOutputFilename(const std::string &filename) {
	char outfile[1024];
	sprintf(outfile, "%s/%s", m_outputDirectory, filename.c_str());
	return outfile;
}

std::ostream *KleeHandler::openOutputFile(const std::string &filename) {
	std::ios::openmode io_mode = std::ios::out | std::ios::trunc
			| std::ios::binary;
	std::ostream *f;
	std::string path = getOutputFilename(filename);
	f = new std::ofstream(path.c_str(), io_mode);
	if (!f) {
		klee_warning("out of memory");
	} else if (!f->good()) {
		klee_warning("error opening: %s", filename.c_str());
		delete f;
		f = NULL;
	}

	return f;
}

std::string KleeHandler::getTestFilename(const std::string &suffix, unsigned id) {
	char filename[1024];
	sprintf(filename, "test%06d.%s", id, suffix.c_str());
	return getOutputFilename(filename);
}

std::ostream *KleeHandler::openTestFile(const std::string &suffix, unsigned id) {
         //this can overflow with a large suffix size
	char filename[1024];
	sprintf(filename, "test%06d.%s", id, suffix.c_str());
	return openOutputFile(filename);
}

/* Outputs all files (.ktest, .pc, .cov etc.) describing a test case */
void KleeHandler::processTestCase(const ExecutionState &state,
		const char *errorMessage, const char *errorSuffix) {
	if (errorMessage && ExitOnError) {
		std::cerr << "EXITING ON ERROR:\n" << errorMessage << "\n";
		exit(1);
	}

	if (!NoOutput) {
		std::vector<std::pair<std::string, std::vector<unsigned char> > > out;
		bool success = m_interpreter->getSymbolicSolution(state, out);

		if (!success)
			klee_warning("unable to get symbolic solution, losing test case");

		double start_time = util::getWallTime();

		unsigned id = ++m_testIndex;

		if (success) {
			KTest b;
			b.numArgs = m_argc;
			b.args = m_argv;
			b.symArgvs = 0;
			b.symArgvLen = 0;
			b.numObjects = out.size();
			b.objects = new KTestObject[b.numObjects];
			assert(b.objects);
			for (unsigned i = 0; i < b.numObjects; i++) {
				KTestObject *o = &b.objects[i];
				o->name = const_cast<char*> (out[i].first.c_str());
				o->numBytes = out[i].second.size();
				o->bytes = new unsigned char[o->numBytes];
				assert(o->bytes);
				std::copy(out[i].second.begin(), out[i].second.end(), o->bytes);
			}

			if (!kTest_toFile(&b, getTestFilename("ktest", id).c_str())) {
				klee_warning("unable to write output test case, losing it");
			}

			for (unsigned i = 0; i < b.numObjects; i++)
				delete[] b.objects[i].bytes;
			delete[] b.objects;
		}

		if (errorMessage) {
			std::ostream *f = openTestFile(errorSuffix, id);
			if(f) {
			  *f << errorMessage;
			  delete f;

			  cloud9::instrum::theInstrManager.recordEvent(cloud9::instrum::ErrorCase,
								       getTestFilename(errorSuffix, id));
			} else {
			  klee_warning("unable to write output test case");
			}
		     
		}

		if (m_pathWriter) {
			std::vector<unsigned char> concreteBranches;
			m_pathWriter->readStream(m_interpreter->getPathStreamID(state),
					concreteBranches);
			std::ostream *f = openTestFile("path", id);
			if(f) {
			  std::copy(concreteBranches.begin(), concreteBranches.end(),
				    std::ostream_iterator<unsigned char>(*f, "\n"));
			  delete f;
			} else {
			  klee_warning("unable to write output test case");
			}
		}

		if (errorMessage || WritePCs) {
			std::string constraints;
			m_interpreter->getConstraintLog(state, constraints);
			std::ostream *f = openTestFile("pc", id);
			if(f) {
			  *f << constraints;
			  delete f;
			} else {
			  klee_warning("unable to write output test case");
			}
		}

		if (WriteCVCs) {
			std::string constraints;
			m_interpreter->getConstraintLog(state, constraints, true);
			std::ostream *f = openTestFile("cvc", id);
			if(f) {
			  *f << constraints;
			  delete f;
			} else {
			  klee_warning("unable to write output test case");
			}
		}

		if (m_symPathWriter) {
			std::vector<unsigned char> symbolicBranches;
			m_symPathWriter->readStream(m_interpreter->getSymbolicPathStreamID(
					state), symbolicBranches);
			std::ostream *f = openTestFile("sym.path", id);
			if(f) {
			  std::copy(symbolicBranches.begin(), symbolicBranches.end(),
				    std::ostream_iterator<unsigned char>(*f, "\n"));
			  delete f;
			}
			else {
			  klee_warning("unable to write output test case");
			}

		}

		if (WriteCov) {
			std::map<const std::string*, std::set<unsigned> > cov;
			m_interpreter->getCoveredLines(state, cov);
			std::ostream *f = openTestFile("cov", id);
			if(f) {
			  for (std::map<const std::string*, std::set<unsigned> >::iterator
				 it = cov.begin(), ie = cov.end(); it != ie; ++it) {
			    for (std::set<unsigned>::iterator it2 = it->second.begin(), ie =
				   it->second.end(); it2 != ie; ++it2)
			      *f << *it->first << ":" << *it2 << "\n";
			  }
			  delete f;
			} else {
			  klee_warning("unable to write output test case");
			}
		}

		if (m_testIndex == StopAfterNTests)
			m_interpreter->setHaltExecution(true);

		if (WriteTestInfo) {
			double elapsed_time = util::getWallTime() - start_time;
			std::ostream *f = openTestFile("info", id);
			if(f) {
			  *f << "Time to generate test case: " << elapsed_time << "s\n";
			  delete f;
			} else {
			  klee_warning("unable to write output test case");
			}
		}

		if (state.coveredNew) {
			cloud9::instrum::InstrumentationManager &manager =
					cloud9::instrum::theInstrManager;

			manager.recordEvent(cloud9::instrum::TestCase,
					"[" +
					manager.stampToString(manager.getRelativeTime(state.lastCoveredTime))
					+ "] " +
					getTestFilename("ktest", id));
		} else {
			cloud9::instrum::theInstrManager.recordEvent(cloud9::instrum::TestCase,
					getTestFilename("ktest", id));
		}
	}
}

// load a .path file
void KleeHandler::loadPathFile(std::string name, std::vector<bool> &buffer) {
	std::ifstream f(name.c_str(), std::ios::in | std::ios::binary);

	if (!f.good())
		assert(0 && "unable to open path file");

	while (f.good()) {
		unsigned value;
		f >> value;
		buffer.push_back(!!value);
		f.get();
	}
}

void KleeHandler::getOutFiles(std::string path,
		std::vector<std::string> &results) {
	llvm::sys::Path p(path);
	std::set<llvm::sys::Path> contents;
	std::string error;
	if (p.getDirectoryContents(contents, &error)) {
		std::cerr << "ERROR: unable to read output directory: " << path << ": "
				<< error << "\n";
		exit(1);
	}
	for (std::set<llvm::sys::Path>::iterator it = contents.begin(), ie =
			contents.end(); it != ie; ++it) {
#if (LLVM_VERSION_MAJOR == 2 && LLVM_VERSION_MINOR == 6)
		std::string f = it->toString();
#else
		std::string f = it->str();
#endif
		if (f.substr(f.size() - 6, f.size()) == ".ktest") {
			results.push_back(f);
		}
	}
}

}
