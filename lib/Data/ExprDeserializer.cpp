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

#include "klee/data/ExprDeserializer.h"

#include "klee/ExprBuilder.h"
#include "klee/data/Support.h"

#include "Expr.pb.h"

#include "llvm/Support/CommandLine.h"

#include <glog/logging.h>

#include <vector>
#include <string>

using namespace llvm;

namespace {

cl::opt<bool> DebugExprDeserialization("debug-expr-deserialization",
    cl::desc("Debug expression deserialization"), cl::init(false));

}

namespace klee {

ExprDeserializer::ExprDeserializer(std::istream &is, ExprBuilder &expr_builder,
                                   std::vector<Array*> arrays)
  : stream_(is), expr_builder_(expr_builder) {
  next_expression_set_ = new data::ExpressionSet();

  for (std::vector<Array*>::iterator it = arrays.begin(), ie = arrays.end();
      it != ie; ++it) {
    Array* array = *it;
    unique_arrays_.insert(std::make_pair(array->name, array));
  }
}

ExprDeserializer::~ExprDeserializer() {
  delete next_expression_set_;
}

ref<Expr> ExprDeserializer::ReadNextExpr() {
  if (expr_queue_.empty())
    PopulateExprQueue();

  if (expr_queue_.empty())
    return NULL;

  ref<Expr> result = expr_queue_.front();
  expr_queue_.pop();

  return result;
}


void ExprDeserializer::PopulateExprQueue() {
  while (expr_queue_.empty()) {
    std::string message;
    ReadNextMessage(stream_, message);

    if (message.empty())
      return;

    next_expression_set_->ParseFromString(message);
    if (next_expression_set_->flush_previous_data()) {
      FlushDeserializationCache();
    }

    DeserializeData(next_expression_set_->data());
    for (int i = 0; i < next_expression_set_->expr_id_size(); i++) {
      expr_queue_.push(GetOrDeserializeExpr(next_expression_set_->expr_id(i)));
    }
    LOG_IF(INFO, DebugExprDeserialization) << "Read new chunk of data of size "
        << message.size();
  }
}


void ExprDeserializer::FlushDeserializationCache() {
  deserialized_arrays_.clear();
  deserialized_update_lists_.clear();
  deserialized_expr_nodes_.clear();
}


void ExprDeserializer::DeserializeData(const data::ExpressionData &expr_data) {
  // First, deserialize arrays
  for (int i = 0; i < expr_data.array_size(); i++) {
    const data::Array &ser_array = expr_data.array(i);
    DeserializeArray(ser_array);
  }

  // Second, populate the pending reconstructions
  for (int i = 0; i < expr_data.update_size(); i++) {
    const data::UpdateList &ser_update_list = expr_data.update(i);
    pending_update_lists_.insert(std::make_pair(ser_update_list.id(), &ser_update_list));
  }

  for (int i = 0; i < expr_data.expr_size(); i++) {
    const data::ExprNode &ser_expr_node = expr_data.expr(i);
    pending_expr_nodes_.insert(std::make_pair(ser_expr_node.id(), &ser_expr_node));
  }

  // Now, force the deserialization
  for (int i = 0; i < expr_data.expr_size(); i++) {
    const data::ExprNode &ser_expr_node = expr_data.expr(i);
    GetOrDeserializeExpr(ser_expr_node.id());
  }

  assert(pending_expr_nodes_.empty() && "Unused expression nodes");
  assert(pending_update_lists_.empty() && "Unused update nodes");
}


const Array* ExprDeserializer::DeserializeArray(
    const data::Array &ser_array) {

  Array *array = NULL;

  if (ser_array.has_contents()) {
    std::vector<ref<ConstantExpr> > constantValues;
    constantValues.reserve(ser_array.size());

    for (std::string::const_iterator it = ser_array.contents().begin(),
        ie = ser_array.contents().end(); it != ie; ++it) {
      uint8_t value = (uint8_t)(*it);
      constantValues.push_back(
          cast<ConstantExpr>(expr_builder_.Constant(value, Expr::Int8)));
    }

    array = new Array(ser_array.name(), ser_array.size(),
                      &constantValues.front(), &constantValues.back() + 1);
  } else {
    array = new Array(ser_array.name(), ser_array.size());
  }

  deserialized_arrays_.insert(std::make_pair(ser_array.id(), array));

  LOG_IF(INFO, DebugExprDeserialization) << "Deserialized array '"
      << ser_array.name() << "' of ID=" << ser_array.id();
  return array;
}


ref<Expr> ExprDeserializer::GetOrDeserializeExpr(uint64_t id) {
  ReverseExprMap::iterator dit = deserialized_expr_nodes_.find(id);
  if (dit != deserialized_expr_nodes_.end()) {
    return dit->second;
  }

  PendingExprMap::iterator pit = pending_expr_nodes_.find(id);
  assert(pit != pending_expr_nodes_.end());
  ref<Expr> result = DeserializeExpr(*pit->second);
  pending_expr_nodes_.erase(id);

  deserialized_expr_nodes_.insert(std::make_pair(id, result));

  return result;
}


const UpdateNode *ExprDeserializer::GetOrDeserializeUpdateNode(
    const Array *array, uint64_t id, uint32_t offset) {
  ReverseUpdateListMap::iterator dit = deserialized_update_lists_.find(id);
  if (dit != deserialized_update_lists_.end())
    return GetUpdateNodeAtOffset(dit->second, offset);

  PendingUpdateListMap::iterator pit = pending_update_lists_.find(id);
  assert(pit != pending_update_lists_.end());
  UpdateList result = DeserializeUpdateList(array, *pit->second);
  pending_update_lists_.erase(id);

  deserialized_update_lists_.insert(std::make_pair(id, result));

  return GetUpdateNodeAtOffset(result, offset);
}


const UpdateNode *ExprDeserializer::GetUpdateNodeAtOffset(const UpdateList &ul,
                                                          uint32_t offset) {
  for (const UpdateNode *un = ul.head; un; un = un->next) {
    if (offset == 0)
      return un;
    offset--;
  }

  assert(0 && "FIXME: Unreachable");
  return NULL;
}


ref<Expr> ExprDeserializer::DeserializeExpr(
    const data::ExprNode &ser_expr_node) {
  switch (ser_expr_node.kind()) {
  // Primitive
  case data::CONSTANT:
    return expr_builder_.Constant(ser_expr_node.value(), ser_expr_node.width());

    // Special
  case data::NOT_OPTIMIZED:
    return expr_builder_.NotOptimized(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)));

  case data::READ: {
    const Array *array = GetArray(ser_expr_node.array_id());
    const UpdateNode *update_node = NULL;
    if (ser_expr_node.has_update_list_id()) {
      update_node = GetOrDeserializeUpdateNode(array,
          ser_expr_node.update_list_id(),
          ser_expr_node.update_list_offset());
    }
    return expr_builder_.Read(UpdateList(array, update_node),
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)));
  }

  case data::SELECT:
    return expr_builder_.Select(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)),
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(1)),
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(2)));

  case data::CONCAT:
    assert(ser_expr_node.child_expr_id_size() == 2 && "FIXME");
    return expr_builder_.Concat(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)),
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(1)));

  case data::EXTRACT:
    return expr_builder_.Extract(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)),
        ser_expr_node.offset(),
        ser_expr_node.width());

  // Casting
  case data::ZEXT:
    return expr_builder_.ZExt(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)),
        ser_expr_node.width());

  case data::SEXT:
    return expr_builder_.SExt(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)),
        ser_expr_node.width());

  // Unary
  case data::NOT:
    return expr_builder_.Not(
        GetOrDeserializeExpr(ser_expr_node.child_expr_id(0)));

  default:
    break;
  }

  ref<Expr> left_expr = GetOrDeserializeExpr(ser_expr_node.child_expr_id(0));
  ref<Expr> right_expr = GetOrDeserializeExpr(ser_expr_node.child_expr_id(1));

  switch (ser_expr_node.kind()) {
  // Arithmetic
  case data::ADD:
    return expr_builder_.Add(left_expr, right_expr);
  case data::SUB:
    return expr_builder_.Sub(left_expr, right_expr);
  case data::MUL:
    return expr_builder_.Mul(left_expr, right_expr);
  case data::UDIV:
    return expr_builder_.UDiv(left_expr, right_expr);
  case data::SDIV:
    return expr_builder_.SDiv(left_expr, right_expr);
  case data::UREM:
    return expr_builder_.URem(left_expr, right_expr);
  case data::SREM:
    return expr_builder_.SRem(left_expr, right_expr);

  // Bit
  case data::AND:
    return expr_builder_.And(left_expr, right_expr);
  case data::OR:
    return expr_builder_.Or(left_expr, right_expr);
  case data::XOR:
    return expr_builder_.Xor(left_expr, right_expr);
  case data::SHL:
    return expr_builder_.Shl(left_expr, right_expr);
  case data::LSHR:
    return expr_builder_.LShr(left_expr, right_expr);
  case data::ASHR:
    return expr_builder_.AShr(left_expr, right_expr);

  // Compare
  case data::EQ:
    return expr_builder_.Eq(left_expr, right_expr);
  case data::NE:
    return expr_builder_.Ne(left_expr, right_expr);
  case data::ULT:
    return expr_builder_.Ult(left_expr, right_expr);
  case data::ULE:
    return expr_builder_.Ule(left_expr, right_expr);
  case data::UGT:
    return expr_builder_.Ugt(left_expr, right_expr);
  case data::UGE:
    return expr_builder_.Uge(left_expr, right_expr);
  case data::SLT:
    return expr_builder_.Slt(left_expr, right_expr);
  case data::SLE:
    return expr_builder_.Sle(left_expr, right_expr);
  case data::SGT:
    return expr_builder_.Sgt(left_expr, right_expr);
  case data::SGE:
    return expr_builder_.Sge(left_expr, right_expr);
  default:
    assert(0 && "Unsupported node kind");
  }
}


UpdateList ExprDeserializer::DeserializeUpdateList(const Array *array,
    const data::UpdateList &ser_update_node) {

  const UpdateNode *next_node = NULL;
  if (ser_update_node.has_next_update_id()) {
    next_node = GetOrDeserializeUpdateNode(array,
                                           ser_update_node.next_update_id(),
                                           ser_update_node.next_update_offset());
  }

  UpdateList result(array, next_node);

  for (int i = ser_update_node.index_expr_id_size() - 1; i >= 0; --i) {
    ref<Expr> index = GetOrDeserializeExpr(ser_update_node.index_expr_id(i));
    ref<Expr> value = GetOrDeserializeExpr(ser_update_node.value_expr_id(i));

    result.extend(index, value);
  }

  return result;
}

}
