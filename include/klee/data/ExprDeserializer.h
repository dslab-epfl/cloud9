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


#ifndef KLEE_DATA_EXPRDESERIALIZER_H_
#define KLEE_DATA_EXPRDESERIALIZER_H_

#include "klee/Expr.h"
#include "klee/util/ExprHashMap.h"

#include "klee/data/Expr.pb.h"

#include <istream>
#include <map>
#include <queue>
#include <vector>

namespace klee {

class ExprBuilder;

class ExprDeserializer {
public:
  ExprDeserializer(std::istream &is, ExprBuilder &expr_builder)
    : stream_(is), expr_builder_(expr_builder) {

  }

  ref<Expr> ReadNextExpr();

private:
  typedef std::map<uint64_t, const Array*> ReverseArrayMap;
  typedef std::map<uint64_t, UpdateList> ReverseUpdateListMap;
  typedef std::map<uint64_t, ref<Expr> > ReverseExprMap;

  typedef std::map<std::string, const Array*> UniqueArrayMap;

  typedef std::map<uint64_t, const data::ExprNode*> PendingExprMap;
  typedef std::map<uint64_t, const data::UpdateNode*> PendingUpdateNodeMap;

  void DeserializeData(const data::ExpressionData &expr_data);

  void PopulateExprQueue();
  void FlushDeserializationCache();

  const Array* GetArray(uint64_t id) {
    return deserialized_arrays_.find(id)->second;
  }
  ref<Expr> GetOrDeserializeExpr(uint64_t id);
  const UpdateNode *GetOrDeserializeUpdateNode(const Array *array, uint64_t id);

  const Array* DeserializeArray(const data::Array &ser_array);
  ref<Expr> DeserializeExpr(const data::ExprNode &ser_expr_node);
  const UpdateNode *DeserializeUpdateNode(const Array *array, const data::UpdateNode &ser_update_node);

  std::istream &stream_;
  std::queue<ref<Expr> > expr_queue_;
  ExprBuilder &expr_builder_;
  data::ExpressionSet next_expression_set_;

  // Not yet serialized IDs, part of the current batch of data
  PendingUpdateNodeMap pending_update_nodes_;
  PendingExprMap pending_expr_nodes_;

  // Mapping used for looking up IDs
  ReverseArrayMap deserialized_arrays_;
  ReverseUpdateListMap deserialized_update_lists_;
  ReverseExprMap deserialized_expr_nodes_;

  // Sets used to ensure structural uniqueness. This avoids duplicates when rendering
  ExprHashSet unique_expr_;
  UpdateListHashSet unique_update_lists_;
  UniqueArrayMap unique_arrays_;

  // Used to keep ref. counters up for yet-unused structures
  std::vector<ref<Expr> > unused_expr_;
};

}



#endif  // KLEE_DATA_EXPRDESERIALIZER_H_
