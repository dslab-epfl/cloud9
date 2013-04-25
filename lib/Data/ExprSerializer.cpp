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

#include "klee/data/ExprSerializer.h"
#include "klee/data/Support.h"

#include "Expr.pb.h"

#include <glog/logging.h>

namespace klee {

ExprSerializer::ExprSerializer(std::ostream &os)
    : stream_(os), next_id_(1), set_flush_flag_(false) {
  expr_set_ = new data::ExpressionSet();
}

ExprSerializer::~ExprSerializer() {
  delete expr_set_;
}

void ExprSerializer::RecordExpr(const ref<Expr> e) {
  uint64_t expr_id = GetOrSerializeExpr(e);

  expr_set_->add_expr_id(expr_id);
}


uint64_t ExprSerializer::GetOrSerializeExpr(const ref<Expr> e) {
  ExprHashMap<uint64_t>::iterator it = serialized_expr_.find(e);

  if (it != serialized_expr_.end()) {
    return it->second;
  }

  uint64_t result = SerializeExpr(e);
  serialized_expr_.insert(std::make_pair(e, result));
  return result;
}


uint64_t ExprSerializer::GetOrSerializeArray(const Array *array) {
  ArrayMap::iterator it = serialized_arrays_.find(array);

  if (it != serialized_arrays_.end()) {
    return it->second;
  }

  uint64_t result = SerializeArray(array);
  serialized_arrays_.insert(std::make_pair(array, result));
  return result;
}


ExprSerializer::UpdateNodePosition ExprSerializer::GetOrSerializeUpdateList(
    const UpdateList &update_list) {
  assert(update_list.head && "Cannot serialize empty update list");

  const UpdateNode *head_node = update_list.head;
  UpdateNodeMap::iterator it = serialized_update_nodes_.find(head_node);

  if (it != serialized_update_nodes_.end()) {
    return it->second;
  }

  data::UpdateList *ser_update_list = expr_set_->mutable_data()->add_update();
  ser_update_list->set_id(next_id_++);

  uint32_t offset = 0;

  for (const UpdateNode *update_node = head_node; update_node;
      update_node = update_node->next) {
    it = serialized_update_nodes_.find(update_node);

    if (it != serialized_update_nodes_.end()) {
      ser_update_list->set_next_update_id(it->second.first);
      ser_update_list->set_next_update_offset(it->second.second);
      break;
    } else {
      ser_update_list->add_index_expr_id(
          GetOrSerializeExpr(update_node->index));
      ser_update_list->add_value_expr_id(
          GetOrSerializeExpr(update_node->value));
      serialized_update_nodes_.insert(
          std::make_pair(update_node, std::make_pair(ser_update_list->id(),
                                                     offset++)));
    }
  }

  return std::make_pair(ser_update_list->id(), 0);
}


uint64_t ExprSerializer::SerializeExpr(const ref<Expr> e) {
  data::ExprNode *ser_expr_node = expr_set_->mutable_data()->add_expr();
  ser_expr_node->set_id(next_id_++);
  ser_expr_node->set_kind((data::ExprKind)e->getKind());

  switch (e->getKind()) {
  case Expr::Constant: {
    ConstantExpr *ce = cast<ConstantExpr>(e);

    assert(ce->getWidth() <= 64 && "FIXME");
    ser_expr_node->set_value(ce->getZExtValue());
    ser_expr_node->set_width(ce->getWidth());
    break;
  }

  case Expr::NotOptimized: {
    NotOptimizedExpr *noe = cast<NotOptimizedExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(noe->src));
    break;
  }

  case Expr::Read: {
    ReadExpr *re = cast<ReadExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(re->index));

    if (re->updates.head) {
      UpdateNodePosition un_pos = GetOrSerializeUpdateList(re->updates);
      ser_expr_node->set_update_list_id(un_pos.first);
      ser_expr_node->set_update_list_offset(un_pos.second);
    }
    ser_expr_node->set_array_id(GetOrSerializeArray(re->updates.root));
    break;
  }

  case Expr::Select: {
    SelectExpr *se = cast<SelectExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(se->cond));
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(se->trueExpr));
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(se->falseExpr));
    break;
  }

  case Expr::Concat: {
    ConcatExpr *ce = cast<ConcatExpr>(e);
    for (unsigned i = 0; i < ce->getNumKids(); ++i) {
      ser_expr_node->add_child_expr_id(GetOrSerializeExpr(ce->getKid(i)));
    }
    break;
  }

  case Expr::Extract: {
    ExtractExpr *ee = cast<ExtractExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(ee->expr));
    ser_expr_node->set_offset(ee->offset);
    ser_expr_node->set_width(ee->getWidth());
    break;
  }

    // Casting,
  case Expr::ZExt:
  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(ce->src));
    ser_expr_node->set_width(ce->getWidth());
    break;
  }

    // All subsequent kinds are binary.

    // Arithmetic
  case Expr::Add:
  case Expr::Sub:
  case Expr::Mul:
  case Expr::UDiv:
  case Expr::SDiv:
  case Expr::URem:
  case Expr::SRem: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->left));
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->right));
    break;
  }

    // Bit
  case Expr::Not: {
    NotExpr *ne = cast<NotExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(ne->expr));
    break;
  }

  case Expr::And:
  case Expr::Or:
  case Expr::Xor:
  case Expr::Shl:
  case Expr::LShr:
  case Expr::AShr: {
    BinaryExpr *be = cast<BinaryExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->left));
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->right));
    break;
  }

    // Compare
  case Expr::Eq:
  case Expr::Ne:  ///< Not used in canonical form
  case Expr::Ult:
  case Expr::Ule:
  case Expr::Ugt: ///< Not used in canonical form
  case Expr::Uge: ///< Not used in canonical form
  case Expr::Slt:
  case Expr::Sle:
  case Expr::Sgt: ///< Not used in canonical form
  case Expr::Sge: { ///< Not used in canonical form
    BinaryExpr *be = cast<BinaryExpr>(e);
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->left));
    ser_expr_node->add_child_expr_id(GetOrSerializeExpr(be->right));
    break;
  }

  default:
    assert(0 && "Unhandled Expr type");
  }

  return ser_expr_node->id();
}


uint64_t ExprSerializer::SerializeArray(const Array *array) {
  data::Array *ser_array = expr_set_->mutable_data()->add_array();
  ser_array->set_id(next_id_++);
  ser_array->set_name(array->name);
  ser_array->set_size(array->size);

  if (!array->constantValues.empty()) {
    // Populate the constant array
    uint8_t *contents = new uint8_t[array->size];
    for (unsigned i = 0; i < array->size; i++)
      contents[i] = array->constantValues[i]->getZExtValue(8);
    ser_array->set_contents(contents, array->size);
    delete []contents;
  }

  return ser_array->id();
}


void ExprSerializer::Flush() {
  if (set_flush_flag_) {
    expr_set_->set_flush_previous_data(true);
    set_flush_flag_ = false;
  }

  WriteProtoMessage(*expr_set_, stream_, true);
  expr_set_->Clear();
  ClearCache();
}


void ExprSerializer::ClearCache() {
  serialized_expr_.clear();
  serialized_arrays_.clear();
  serialized_update_nodes_.clear();
  set_flush_flag_ = true;
}

}
