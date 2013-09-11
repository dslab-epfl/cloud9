/*
 * Cloud9 Parallel Symbolic Execution Engine
 *
 * Copyright 2012 Google Inc. All Rights Reserved.
 * Author: sbucur@google.com (Stefan Bucur)
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

#include "llvm/Support/CommandLine.h"

#include "klee/ExprBuilder.h"
#include "klee/data/ExprDeserializer.h"
#include "klee/data/ExprVisualizer.h"

#include <glog/logging.h>
#include <google/protobuf/stubs/common.h>

#include <iostream>
#include <fstream>

using namespace klee;
using namespace llvm;

namespace {
cl::opt<std::string> InputFileName(cl::Positional,
    cl::desc("<input expression log file>"),
    cl::Required);

}

int main(int argc, char **argv, char **envp) {
  GOOGLE_PROTOBUF_VERIFY_VERSION;
  google::InitGoogleLogging(argv[0]);
  cl::ParseCommandLineOptions(argc, argv, "Expression file processor");

  std::ifstream is(InputFileName.c_str());
  if (is.fail()) {
    LOG(FATAL) << "Cannot open expression data file '" << InputFileName << "'";
  }
  LOG(INFO) << "Processing '" << InputFileName << "'";

  ExprBuilder *expr_builder = createDefaultExprBuilder();
  ExprDeserializer expr_deserializer(is, *expr_builder, std::vector<Array*>());

  ref<Expr> current_expr = expr_deserializer.ReadNextExpr();

  DefaultExprDotDecorator decorator;
  ExprVisualizer visualizer(std::cout, decorator);

  std::vector<ref<Expr> > expressions;

  visualizer.BeginDrawing();

  while (!current_expr.isNull()) {
    expressions.push_back(current_expr);
    current_expr = expr_deserializer.ReadNextExpr();
  }

  LOG(INFO) << "Deserialized " << expressions.size() << " expressions.";

  if (expressions.size() >= 2) {
    decorator.HighlightExpr(expressions[expressions.size()-2], "Second to Last");
    decorator.HighlightExpr(expressions[expressions.size()-1], "Last");
    visualizer.DrawExpr(expressions[expressions.size()-2]);
    visualizer.DrawExpr(expressions[expressions.size()-1]);
  }

  visualizer.EndDrawing();

  delete expr_builder;
}
