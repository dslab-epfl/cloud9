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

#ifndef KLEE_DATA_EXPRVISUALIZER_H_
#define KLEE_DATA_EXPRVISUALIZER_H_

#include "klee/Expr.h"

#include "klee/util/ExprHashMap.h"
#include "klee/util/ExprVisitor.h"

#include <ostream>
#include <string>
#include <map>
#include <set>
#include <stack>

namespace klee {

class ExprConstantDecorator {
public:
  ExprConstantDecorator() { }

  ref<Expr> GetConstantValuation(const ref<Expr> expr);

private:
  ExprHashMap<ref<Expr> > constant_mapping_;
};

class ExprDotDecorator {
public:
  typedef std::map<std::string, std::string> PropertyMap;
  typedef std::vector<std::pair<ref<Expr>, PropertyMap > > EdgeDecoration;

  ExprDotDecorator() { }
  virtual ~ExprDotDecorator() { }

  virtual std::string GetExprKindLabel(const ref<Expr> expr);
  virtual std::string GetConstantLabel(const ref<Expr> expr);

  virtual void DecorateExprNode(const ref<Expr> expr, bool is_mask,
      const ref<Expr> value, PropertyMap &properties) = 0;
  virtual void DecorateExprEdges(const ref<Expr> expr,
      EdgeDecoration &edge_decoration) = 0;

  virtual void DecorateArray(const Array *array, PropertyMap &properties) = 0;
};

// TODO(sbucur): Convert to visitor pattern
class ExprVisualizer {
public:
  ExprVisualizer(std::ostream &os, ExprDotDecorator &decorator)
    : stream_(os), decorator_(decorator), next_expr_id_(0) {

  }

  void BeginDrawing();
  void DrawExpr(const ref<Expr> expr);
  void MaskExpr(const ref<Expr> expr);
  void EndDrawing();

private:
  typedef std::map<const UpdateNode*, std::string> UpdateNodeNameMap;

  void DrawUpdateList(const UpdateList &ul, std::stack<ref<Expr> > &expr_stack);
  void MaskUpdateList(const UpdateList &ul, std::stack<ref<Expr> > &expr_stack);
  void DrawArray(const Array *array);

  void PrintPropertyMap(const ExprDotDecorator::PropertyMap &properties);
  void PrintGraphNode(const std::string &name,
      const ExprDotDecorator::PropertyMap &properties);
  void PrintGraphEdge(const std::string &from, const std::string &to,
      const ExprDotDecorator::PropertyMap &properties);

  std::string GetExprName(const ref<Expr> expr);
  std::string GetUpdateNodeName(const UpdateNode *un);
  std::string GetArrayCellName(const Array *array, unsigned index);

  std::ostream &stream_;
  ExprDotDecorator &decorator_;

  ExprHashMap<std::string> named_expr_;  // Expr that have a name
  UpdateNodeNameMap named_update_nodes_;  // Update nodes that have a name

  ExprHashSet processed_expr_;  // Expr already printed
  ExprHashSet masked_expr_; // Expr not printed, but masked

  std::set<const Array*> processed_arrays_;  // Arrays already processed

  std::set<const UpdateNode*> processed_update_nodes_;  // Update nodes already processed
  std::set<const UpdateNode*> masked_update_nodes_;

  uint64_t next_expr_id_;
};

class DefaultExprDotDecorator: public ExprDotDecorator {
public:
  DefaultExprDotDecorator() { }

  void HighlightExpr(const ref<Expr> expr, std::string label);

  virtual void DecorateExprNode(const ref<Expr> expr, bool is_mask,
      const ref<Expr> value, PropertyMap &properties);
  virtual void DecorateExprEdges(const ref<Expr> expr,
      EdgeDecoration &edge_decoration);

  virtual void DecorateArray(const Array *array, PropertyMap &properties);

private:
  ExprHashMap<std::string> highlights_;
};

}


#endif /* KLEE_DATA_EXPRVISUALIZER_H_ */
