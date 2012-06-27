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

#include <iostream>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <assert.h>
#include <sys/stat.h>

#include <boost/scoped_ptr.hpp>
#include <boost/foreach.hpp>

#include "expr/Lexer.h"
#include "expr/Parser.h"
#include "klee/Constraints.h"
#include "klee/Solver.h"
#include "klee/util/ExprUtil.h"
#include "../../lib/Solver/STPBuilder.h"

#include "klee/Expr.h"
#include "klee/ExprBuilder.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"
#include "llvm/ADT/OwningPtr.h"

#include "TmpFile.h"

using namespace llvm;
using namespace klee;
using namespace klee::expr;
using namespace std;
using namespace boost;

#ifdef HAVE_EXT_STP
namespace {

cl::opt<string> inputFileName(
    cl::desc("<input file name with klee queries>"),
    cl::Positional,
    cl::init("queries.pc"));

cl::opt<string> outputDir(
    cl::desc("<output directory>"),
    cl::Positional,
    cl::init("converted-queries"));

cl::opt<bool>
  runTests("run-tests",
               cl::desc("run tests"),
               cl::init(false));
cl::opt<bool>
    printToStdout("print-to-stdout",
               cl::desc("print result to stdout"),
               cl::init(false));

cl::opt<bool>
    optimizeDivides("optimize-divides",
            cl::desc("optimize divides in original queries: not always good"),
            cl::init(false));
}

class OutStreamFactoryBase {
public:
    virtual ostream* Create(int queryNumber) const = 0;
};

class OutStreamFactory: public OutStreamFactoryBase {
public:
    OutStreamFactory(const string& dirPath) : _dirPath(dirPath) { }

    bool Init() {
        if (dirExists()) {
          return false;
        }
        return createDir();
    }

    ostream* Create(int queryNumber) const {
        stringstream fileName;
        fileName << _dirPath << "/query" << queryNumber << ".smt";
        ofstream* out = new ofstream(fileName.str().c_str(), ios::out | ios::app);
        if(!out->is_open())
            return NULL;

        return out;
    }

private:
    const string _dirPath;

    bool dirExists() const {
        struct stat st;
        return stat(_dirPath.c_str(), &st) == 0;
    }

    bool createDir() const {
        return mkdir(_dirPath.c_str(), 0777) == 0;
    }
};

class StdOutStreamFactory: public OutStreamFactoryBase {
public:
    StdOutStreamFactory() { }

    ostream* Create(int queryNumber) const {
        stringstream fileName;
        ofstream* out = new ofstream(fileName.str().c_str(), ios::out | ios::app);
        if(!out->is_open())
            return NULL;

        return out;
    }
};


static bool parseDeclarations(const string& filename, vector<Decl*>& declarations) {
  string error;
  OwningPtr<MemoryBuffer> memoryBuffer;
  error_code ec = MemoryBuffer::getFileOrSTDIN(filename.c_str(), memoryBuffer);
  if(memoryBuffer.get() == NULL)
    return false;

  scoped_ptr<ExprBuilder> exprBuilder(createDefaultExprBuilder());
  scoped_ptr<Parser> parser(Parser::Create(filename.c_str(), memoryBuffer.get(), exprBuilder.get()));
  parser->SetMaxErrors(20);

  while (Decl *d = parser->ParseTopLevelDecl())
    declarations.push_back(d);

  return parser->GetNumErrors() == 0;
}

static ref<Expr>andConstraints(const std::vector<ref<Expr> >& constraints) {
  boost::scoped_ptr<ExprBuilder> exprBuilder(createDefaultExprBuilder());

  ref<Expr> result = exprBuilder->True();

  BOOST_FOREACH(const ref<Expr>& expr, constraints) {
    result = exprBuilder->And(result, expr);
  }

  return result;
}

static ref<Expr> andConstraintsNotQuery(const std::vector<ref<Expr> >& constraints, const ref<Expr>& query) {
  boost::scoped_ptr<ExprBuilder> exprBuilder(createDefaultExprBuilder());

  return exprBuilder->And(andConstraints(constraints), exprBuilder->Not(query));
}

static bool convertDeclarations(const vector<Decl*>& kleeQueries,
    vector<string>& smtQueries,
    bool optimizeDivides) {

  VC vc = vc_createValidityChecker();
  scoped_ptr<STPBuilder> stpBuilder(new STPBuilder(vc, optimizeDivides));

  BOOST_FOREACH(Decl* decl, kleeQueries) {
      QueryCommand *queryCmd = dyn_cast<QueryCommand>(decl);
      if(queryCmd == NULL)
          continue;

      //unsat will mean valid
      ref<Expr> queryToCheck = andConstraintsNotQuery(queryCmd->Constraints, queryCmd->Query);

      klee::ExprHandle exprHandle = stpBuilder->construct(queryToCheck);
      scoped_ptr<char> str(vc_printSMTLIB(vc, exprHandle));
      smtQueries.push_back(string(str.get()));
  }

  return true;
}


static bool writeTo(const OutStreamFactoryBase& outStreamCreator, const vector<string>& content) {
  int i = 0;
  BOOST_FOREACH(const string& s, content) {
    scoped_ptr<ostream> out(outStreamCreator.Create(i++));
    if(out.get() == NULL)
      return false;

    *out << s;
  }

  return true;
}

static void writeToStdOut(const vector<string>& content) {
  BOOST_FOREACH(const string& s, content) {
    std::cout << s;
  }
}


static int runAllTests() {
  cout << "running tests.." << endl;
  TmpFile tmpFile;
  stringstream ss;
  ss << "# Query 5 -- Type: InitialValues, StateID: 0x0, Instructions: 50368\n\
array arr4_input2[4] : w32 -> w8 = symbolic\n\
array arr3_input1[4] : w32 -> w8 = symbolic\n\
(query [(Slt 0\n\
             N0:(ReadLSB w32 0 arr3_input1))\n\
        (Slt 9\n\
             N1:(ReadLSB w32 0 arr4_input2))]\n\
       (Slt N1 (Shl w32 N0 1)) []\n\
       [arr4_input2\n\
        arr3_input1])\n\
#   OK -- Elapsed: 0.00218391\n\
#   Solvable: true\n\
#     arr4_input2 = [0,0,0,1]\n\
#     arr3_input1 = [0,0,0,66]";
  tmpFile.write(ss.str());
  ifstream in(tmpFile.getTmpFileName().c_str());

  vector<Decl*> declarations;
  assert(
      parseDeclarations(tmpFile.getTmpFileName(), declarations));

  assert(
      declarations.size() == 3); //2 arrays, 1 query

  vector<string> smtQueries;
  assert(
      convertDeclarations(declarations, smtQueries, true) && "conversion error");

  assert(
      smtQueries.size() == 1);

  cout << "OK" << endl;
  return 0;
}


int main(int argc, char** argv) {
  cl::ParseCommandLineOptions(argc, argv, "Kleaver (stp-queries.qlog) -> smt converter. Don't use with pc query files.");

  if (runTests)
    return runAllTests();

  vector<Decl*> kleeQueries;
  assert(
      parseDeclarations(inputFileName, kleeQueries) && "parsing error");

  vector<string> smtQueries;
  assert(
      convertDeclarations(kleeQueries, smtQueries, optimizeDivides.getValue()) && "conversion error");

  BOOST_FOREACH(Decl* d, kleeQueries) {
    delete d;
  }

  if (printToStdout.getValue()) {
    writeToStdOut(smtQueries);
    return 0;
  }

  OutStreamFactory outStreamFactory(outputDir);
  assert(
      outStreamFactory.Init() && "Init output failed");
  assert(
      writeTo(outStreamFactory, smtQueries) && "problems while writing");

  return 0;
}
#else
int main(int argc, char** argv) {
  std::cout << "kleaver2smt converter requires external STP" << std::endl;
  return 0;
}
#endif
