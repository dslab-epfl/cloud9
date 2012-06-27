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

#include "klee/data/ExprVisualizer.h"

#include "llvm/Support/CommandLine.h"

#include <ostream>
#include <sstream>
#include <stack>

using namespace llvm;

namespace {
cl::opt<unsigned> ArrayWrapSize("array-wrap-size",
                           cl::desc("The maximum width of an array when visualizing"),
                           cl::init(64));

cl::opt<bool> ShortcutConstReads("shortcut-const-reads",
                                 cl::desc("Make const read accesses point to the array cell"),
                                 cl::init(false));
}

namespace klee {

////////////////////////////////////////////////////////////////////////////////
// ExprConstantDecorator
////////////////////////////////////////////////////////////////////////////////

ref<Expr> ExprConstantDecorator::GetConstantValuation(const ref<Expr> expr) {
  if (isa<ConstantExpr>(expr))
    return expr;

  ExprHashMap<ref<Expr> >::iterator it = constant_mapping_.find(expr);
  if (it != constant_mapping_.end())
    return it->second;

  if (expr->getKind() == Expr::Read) {
    ReadExpr *re = cast<ReadExpr>(expr);
    ref<Expr> const_index = GetConstantValuation(re->index);

    if (const_index.isNull()) {
      constant_mapping_.insert(std::make_pair(expr, ref<Expr>()));
      return NULL;
    }

    unsigned index = cast<ConstantExpr>(const_index)->getZExtValue();

    for (const UpdateNode *un = re->updates.head; un; un = un->next) {
      ref<Expr> ui = GetConstantValuation(un->index);

      if (ui.isNull()) {
        constant_mapping_.insert(std::make_pair(expr, ref<Expr>()));
        return NULL;
      }

      if (ConstantExpr *CE = dyn_cast<ConstantExpr>(ui)) {
        if (CE->getZExtValue() == index) {
          ref<Expr> const_value = GetConstantValuation(un->value);
          constant_mapping_.insert(std::make_pair(expr, const_value));
          return const_value;
        }
      } else {
        constant_mapping_.insert(std::make_pair(expr, ref<Expr>()));
        return NULL;
      }
    }

    if (re->updates.root->isConstantArray() && index < re->updates.root->size) {
      ref<Expr> const_value = re->updates.root->constantValues[index];
      constant_mapping_.insert(std::make_pair(expr, const_value));
      return const_value;
    }

    constant_mapping_.insert(std::make_pair(expr, ref<Expr>()));
    return NULL;
  }

  std::vector<ref<Expr> > kids;
  kids.reserve(4);

  for (unsigned i = 0; i < expr->getNumKids(); i++) {
    ref<Expr> const_kid = GetConstantValuation(expr->getKid(i));
    if (const_kid.isNull()) {
      constant_mapping_.insert(std::make_pair(expr, ref<Expr>()));
      return NULL;
    }
    kids.push_back(const_kid);
  }

  ref<Expr> const_expr = expr->rebuild(&kids[0]);
  constant_mapping_.insert(std::make_pair(expr, const_expr));
  return const_expr;
}

////////////////////////////////////////////////////////////////////////////////
// ExprDotDecorator
////////////////////////////////////////////////////////////////////////////////


std::string ExprDotDecorator::GetConstantLabel(const ref<Expr> expr) {
  ConstantExpr *ce = cast<ConstantExpr>(expr);

  std::ostringstream oss;
  oss << "\"" << "w" << ce->getWidth() << ": " << ce->getZExtValue() << "\"";

  return oss.str();
}


std::string ExprDotDecorator::GetExprKindLabel(const ref<Expr> expr) {
  switch (expr->getKind()) {
  case Expr::Constant:
    return "CONST";
  case Expr::NotOptimized:
    return "NOPT";
  case Expr::Read:
    return "READ";
  case Expr::Select:
    return "SEL";
  case Expr::Concat:
    return "CNCT";
  case Expr::Extract:
    return "XTCT";

    // Casting
  case Expr::ZExt:
    return "ZEXT";
  case Expr::SExt:
    return "SEXT";

    // Arithmetic
  case Expr::Add:
    return "ADD";
  case Expr::Sub:
    return "SUB";
  case Expr::Mul:
    return "MUL";
  case Expr::UDiv:
    return "UDIV";
  case Expr::SDiv:
    return "SDIV";
  case Expr::URem:
    return "UREM";
  case Expr::SRem:
    return "SREM";

    // Bit
  case Expr::Not:
    return "NOT";
  case Expr::And:
    return "AND";
  case Expr::Or:
    return "OR";
  case Expr::Xor:
    return "XOR";
  case Expr::Shl:
    return "SHL";
  case Expr::LShr:
    return "LSHR";
  case Expr::AShr:
    return "ASHR";

    // Compare
  case Expr::Eq:
    return "EQ";
  case Expr::Ne:  ///< Not used in canonical form
    return "NE";
  case Expr::Ult:
    return "ULT";
  case Expr::Ule:
    return "ULE";
  case Expr::Ugt: ///< Not used in canonical form
    return "UGT";
  case Expr::Uge: ///< Not used in canonical form
    return "UGE";
  case Expr::Slt:
    return "SLT";
  case Expr::Sle:
    return "SLE";
  case Expr::Sgt: ///< Not used in canonical form
    return "SGT";
  case Expr::Sge:
    return "SGE";

  default:
    assert(0 && "Unhandled Expr type");
  }
}


////////////////////////////////////////////////////////////////////////////////
// ExprVisualizer
////////////////////////////////////////////////////////////////////////////////


void ExprVisualizer::BeginDrawing() {
  stream_ << "digraph expr {" << std::endl;
  stream_ << "  graph [fontname=Helvetica];" << std::endl;
}

void ExprVisualizer::DrawExpr(const ref<Expr> expr) {
  std::stack<ref<Expr> > expr_stack;

  ExprConstantDecorator const_decorator;

  expr_stack.push(expr);

  while (!expr_stack.empty()) {
    ref<Expr> cur_expr = expr_stack.top();
    expr_stack.pop();

    if (processed_expr_.count(cur_expr) > 0)
      continue;

    ExprDotDecorator::PropertyMap expr_properties;
    decorator_.DecorateExprNode(cur_expr, masked_expr_.count(cur_expr) > 0,
        const_decorator.GetConstantValuation(cur_expr), expr_properties);
    PrintGraphNode(GetExprName(cur_expr), expr_properties);

    if (masked_expr_.count(cur_expr) > 0)
      continue;

    for (unsigned i = 0; i < cur_expr->getNumKids(); i++) {
      expr_stack.push(cur_expr->getKid(i));
    }

    if (cur_expr->getKind() == Expr::Read) {
      ReadExpr *re = cast<ReadExpr>(cur_expr);
      ExprDotDecorator::PropertyMap dummy_map;

      if (ShortcutConstReads && !re->updates.head && isa<ConstantExpr>(re->index)) {
        DrawArray(re->updates.root);

        ConstantExpr *ce = cast<ConstantExpr>(re->index);
        PrintGraphEdge(GetExprName(cur_expr),
            GetArrayCellName(re->updates.root, ce->getZExtValue()), dummy_map);
      } else {
        ExprDotDecorator::EdgeDecoration edge_decoration;
        decorator_.DecorateExprEdges(cur_expr, edge_decoration);

        for (ExprDotDecorator::EdgeDecoration::iterator it = edge_decoration.begin(),
            ie = edge_decoration.end(); it != ie; ++it) {
          PrintGraphEdge(GetExprName(cur_expr), GetExprName(it->first), it->second);
        }

#if 0
        DrawUpdateList(re->updates, expr_stack);

        if (re->updates.head) {
          PrintGraphEdge(GetExprName(cur_expr), GetUpdateNodeName(re->updates.head), dummy_map);
        } else {
          PrintGraphEdge(GetExprName(cur_expr), re->updates.root->name, dummy_map);
        }

#else
        DrawArray(re->updates.root);
        PrintGraphEdge(GetExprName(cur_expr), re->updates.root->name, dummy_map);
#endif
      }
    } else {
      ExprDotDecorator::EdgeDecoration edge_decoration;
      decorator_.DecorateExprEdges(cur_expr, edge_decoration);

      for (ExprDotDecorator::EdgeDecoration::iterator it = edge_decoration.begin(),
          ie = edge_decoration.end(); it != ie; ++it) {
        PrintGraphEdge(GetExprName(cur_expr), GetExprName(it->first), it->second);
      }
    }

    processed_expr_.insert(cur_expr);
  }
}


void ExprVisualizer::MaskExpr(const ref<Expr> expr) {
  std::stack<ref<Expr> > expr_stack;

  expr_stack.push(expr);

  while (!expr_stack.empty()) {
    ref<Expr> cur_expr = expr_stack.top();
    expr_stack.pop();

    if (processed_expr_.count(cur_expr) > 0)
      continue;

    if (masked_expr_.count(cur_expr) > 0)
      continue;

    for (unsigned i = 0; i < cur_expr->getNumKids(); i++) {
      expr_stack.push(cur_expr->getKid(i));
    }

    if (cur_expr->getKind() == Expr::Read) {
      ReadExpr *re = cast<ReadExpr>(cur_expr);
      MaskUpdateList(re->updates, expr_stack);
    }

    masked_expr_.insert(cur_expr);
  }
}


void ExprVisualizer::EndDrawing() {
  stream_ << "}" << std::endl;
}


void ExprVisualizer::DrawUpdateList(const UpdateList &ul,
    std::stack<ref<Expr> > &expr_stack) {
  DrawArray(ul.root);

  for (const UpdateNode *un = ul.head; un != NULL; un = un->next) {
    if (processed_update_nodes_.count(un) > 0)
      break;

    ExprDotDecorator::PropertyMap next_edge_properties;

    ExprDotDecorator::PropertyMap node_properties;
    PrintGraphNode(GetUpdateNodeName(un), node_properties);

    if (masked_update_nodes_.count(un) > 0)
      break;

    ExprDotDecorator::EdgeDecoration edge_decoration;

    if (un->next) {
      next_edge_properties["constraint"] = "false";
      PrintGraphEdge(GetUpdateNodeName(un), GetUpdateNodeName(un->next),
          next_edge_properties);
    } else {
      PrintGraphEdge(GetUpdateNodeName(un), ul.root->name, next_edge_properties);
    }

    next_edge_properties.clear();
    PrintGraphEdge(GetUpdateNodeName(un), GetExprName(un->index), next_edge_properties);
    PrintGraphEdge(GetUpdateNodeName(un), GetExprName(un->value), next_edge_properties);

    expr_stack.push(un->index);
    expr_stack.push(un->value);

    processed_update_nodes_.insert(un);
  }
}


void ExprVisualizer::MaskUpdateList(const UpdateList &ul,
    std::stack<ref<Expr> > &expr_stack) {

  for (const UpdateNode *un = ul.head; un != NULL; un = un->next) {
    if (processed_update_nodes_.count(un) > 0)
      break;
    if (masked_update_nodes_.count(un) > 0)
      break;

    ExprDotDecorator::PropertyMap next_edge_properties;

    expr_stack.push(un->index);
    expr_stack.push(un->value);

    masked_update_nodes_.insert(un);
  }
}


void ExprVisualizer::DrawArray(const Array *array) {
  if (processed_arrays_.count(array) > 0)
    return;

  ExprDotDecorator::PropertyMap array_properties;
  decorator_.DecorateArray(array, array_properties);
  PrintGraphNode(array->name, array_properties);

  processed_arrays_.insert(array);
}


std::string ExprVisualizer::GetExprName(const ref<Expr> expr) {
  ExprHashMap<std::string>::iterator it = named_expr_.find(expr);
  if (it != named_expr_.end()) {
    return it->second;
  }

  std::ostringstream oss;
  oss << "E" << (next_expr_id_++);

  std::string name = oss.str();

  named_expr_.insert(std::make_pair(expr, name));

  return name;
}


std::string ExprVisualizer::GetUpdateNodeName(const UpdateNode *un) {
  UpdateNodeNameMap::iterator it = named_update_nodes_.find(un);
  if (it != named_update_nodes_.end())
    return it->second;

  std::ostringstream oss;
  oss << "UN" << (next_expr_id_++);

  std::string name = oss.str();

  named_update_nodes_.insert(std::make_pair(un, name));

  return name;
}

std::string ExprVisualizer::GetArrayCellName(const Array *array, unsigned index) {
  std::ostringstream oss;
  oss << array->name << ":" << array->name << "_" << index;

  return oss.str();
}


void ExprVisualizer::PrintPropertyMap(
    const ExprDotDecorator::PropertyMap &properties) {
  if (!properties.empty()) {
    stream_ << " [";
    for (ExprDotDecorator::PropertyMap::const_iterator it = properties.begin(),
        ie = properties.end(); it != ie; ++it) {
      if (it != properties.begin())
        stream_ << ",";
      stream_ << it->first << "=" << it->second;
    }
    stream_ << "]";
  }
}


void ExprVisualizer::PrintGraphNode(const std::string &name,
    const ExprDotDecorator::PropertyMap &properties) {
  stream_ << "  " << name;
  PrintPropertyMap(properties);
  stream_ << ";" << std::endl;
}


void ExprVisualizer::PrintGraphEdge(const std::string &from,
    const std::string &to, const ExprDotDecorator::PropertyMap &properties) {
  stream_ << "  " << from << " -> " << to;
  PrintPropertyMap(properties);
  stream_ << ";" << std::endl;
}


////////////////////////////////////////////////////////////////////////////////
// Decorators
////////////////////////////////////////////////////////////////////////////////

void DefaultExprDotDecorator::HighlightExpr(const ref<Expr> expr,
    std::string label) {
  highlights_.insert(std::make_pair(expr, label));
}

void DefaultExprDotDecorator::DecorateExprNode(const ref<Expr> expr,
    bool is_mask, const ref<Expr> value, PropertyMap &properties) {

  properties["shape"] = "circle";
  properties["margin"] = "0";

  switch (expr->getKind()) {
  case Expr::Constant:
    properties["label"] = GetConstantLabel(expr);
    properties["shape"] = "box";
    properties["style"] = "filled";
    properties["fillcolor"] = "lightgray";
    break;
  case Expr::Read:
    properties["label"] = GetExprKindLabel(expr);
    properties["shape"] = "box";
    break;
  case Expr::ZExt:
  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(expr);
    std::ostringstream oss;
    oss << "\"" << GetExprKindLabel(expr) << "\\n[" << ce->getWidth() << "]\"";
    properties["label"] = oss.str();
    break;
  }
  default:
    properties["label"] = GetExprKindLabel(expr);
    break;
  }

  if (is_mask) {
    properties["shape"] = "box";
    properties["style"] = "filled";
    properties["fillcolor"] = "dimgray";
  }

  if (highlights_.count(expr) > 0) {
    properties["color"] = "red";
    properties["xlabel"] = highlights_[expr];
  }

  if (!value.isNull() && isa<ConstantExpr>(value)) {
    properties["xlabel"] = GetConstantLabel(value);
  }
}

void DefaultExprDotDecorator::DecorateExprEdges(const ref<Expr> expr,
      EdgeDecoration &edge_decoration) {
  PropertyMap decoration;
  decoration["fontsize"] = "10.0";

  switch (expr->getKind()) {
  case Expr::Constant:
    break;
  case Expr::NotOptimized: {
    NotOptimizedExpr *noe = cast<NotOptimizedExpr>(expr);
    decoration["label"] = "expr";
    edge_decoration.push_back(std::make_pair(noe->src, decoration));
    break;
  }
  case Expr::Read: {
    ReadExpr *re = cast<ReadExpr>(expr);
    decoration["label"] = "index";
    decoration["style"] = "dotted";
    edge_decoration.push_back(std::make_pair(re->index, decoration));
    break;
  }
  case Expr::Select: {
    SelectExpr *se = cast<SelectExpr>(expr);
    decoration["label"] = "cond";
    edge_decoration.push_back(std::make_pair(se->cond, decoration));
    decoration["label"] = "true";
    edge_decoration.push_back(std::make_pair(se->trueExpr, decoration));
    decoration["label"] = "false";
    edge_decoration.push_back(std::make_pair(se->falseExpr, decoration));
    break;
  }
  case Expr::Concat: {
    ConcatExpr *ce = cast<ConcatExpr>(expr);
    for (unsigned i = 0; i < ce->getNumKids(); ++i) {
      std::ostringstream oss;
      oss << i;
      decoration["label"] = oss.str();

      edge_decoration.push_back(std::make_pair(ce->getKid(i), decoration));
    }
    break;
  }
  case Expr::Extract: {
    ExtractExpr *ee = cast<ExtractExpr>(expr);
    decoration["label"] = "expr";
    edge_decoration.push_back(std::make_pair(ee->expr, decoration));
    break;
  }

    // Casting
  case Expr::ZExt:
  case Expr::SExt: {
    CastExpr *ce = cast<CastExpr>(expr);
    decoration["label"] = "src";
    edge_decoration.push_back(std::make_pair(ce->src, decoration));
    break;
  }

    // Arithmetic
  case Expr::Add:
  case Expr::Sub:
  case Expr::Mul:
  case Expr::UDiv:
  case Expr::SDiv:
  case Expr::URem:
  case Expr::SRem: {
    BinaryExpr *be = cast<BinaryExpr>(expr);
    decoration["label"] = "lhs";
    edge_decoration.push_back(std::make_pair(be->left, decoration));
    decoration["label"] = "rhs";
    edge_decoration.push_back(std::make_pair(be->right, decoration));
    break;
  }

    // Bit
  case Expr::Not: {
    NotExpr *ne = cast<NotExpr>(expr);
    decoration["label"] = "expr";
    edge_decoration.push_back(std::make_pair(ne->expr, decoration));
    break;
  }

  case Expr::And:
  case Expr::Or:
  case Expr::Xor:
  case Expr::Shl:
  case Expr::LShr:
  case Expr::AShr: {
    BinaryExpr *be = cast<BinaryExpr>(expr);
    decoration["label"] = "lhs";
    edge_decoration.push_back(std::make_pair(be->left, decoration));
    decoration["label"] = "rhs";
    edge_decoration.push_back(std::make_pair(be->right, decoration));
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
  case Expr::Sge: {
    BinaryExpr *be = cast<BinaryExpr>(expr);
    decoration["label"] = "lhs";
    edge_decoration.push_back(std::make_pair(be->left, decoration));
    decoration["label"] = "rhs";
    edge_decoration.push_back(std::make_pair(be->right, decoration));
    break;
  }

  default:
    assert(0 && "Unhandled Expr type");
  }
}

void DefaultExprDotDecorator::DecorateArray(const Array *array,
    PropertyMap &properties) {
  properties["shape"] = "record";

  // Compose the label
  std::ostringstream oss;
  oss << "\"";
  if (array->size > ArrayWrapSize)
    oss << "{";
  for (unsigned i = 0; i < array->size; i++) {
    if (i > 0) {
      if (array->size > ArrayWrapSize && i % ArrayWrapSize == 0)
        oss << "}";
      oss << " | ";
    }
    if (array->size > ArrayWrapSize && i % ArrayWrapSize == 0) {
      oss << "{";
    }

    oss << "<" << array->name << "_" << i << "> ";
    oss << "[" << i << "]\\n";
    if (!array->constantValues.empty())
      oss << array->constantValues[i]->getZExtValue();
    else
      oss << "X";

    if (i == array->size - 1) {
      if ((i + 1) % ArrayWrapSize != 0)
        oss << "}";
    }
  }
  if (array->size > ArrayWrapSize)
    oss << "}";
  oss << "\"";
  properties["label"] = oss.str();
  properties["xlabel"] = array->name;
}

}
