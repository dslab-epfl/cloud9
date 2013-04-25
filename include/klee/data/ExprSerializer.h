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


#ifndef KLEE_DATA_EXPRSERIALIZER_H_
#define KLEE_DATA_EXPRSERIALIZER_H_

#include "klee/Expr.h"
#include "klee/util/ExprHashMap.h"

#include <ostream>
#include <fstream>
#include <map>

namespace klee {

namespace data {
class ExpressionSet;
}

class ExprSerializer {
public:
  explicit ExprSerializer(std::ostream &os);
  virtual ~ExprSerializer();

  void RecordExpr(const ref<Expr> e);

  void Flush();
private:
  typedef std::pair<uint64_t, uint32_t> UpdateNodePosition;
  typedef std::map<const Array*, uint64_t> ArrayMap;
  typedef std::map<const UpdateNode*, UpdateNodePosition> UpdateNodeMap;

  uint64_t GetOrSerializeExpr(const ref<Expr> e);
  uint64_t GetOrSerializeArray(const Array *array);
  UpdateNodePosition GetOrSerializeUpdateList(const UpdateList &update_list);

  uint64_t SerializeExpr(const ref<Expr> e);
  uint64_t SerializeArray(const Array *array);

  void ClearCache();

  std::ostream &stream_;

  uint64_t next_id_;

  ExprHashMap<uint64_t> serialized_expr_;
  ArrayMap serialized_arrays_;
  UpdateNodeMap serialized_update_nodes_;

  data::ExpressionSet *expr_set_;
  bool set_flush_flag_;
};

}


#endif  // KLEE_DATA_EXPRSERIALIZER_H_
