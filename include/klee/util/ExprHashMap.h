//===-- ExprHashMap.h -------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_EXPRHASHMAP_H
#define KLEE_EXPRHASHMAP_H

#include "klee/Expr.h"
#include <tr1/unordered_map>
#include <tr1/unordered_set>

namespace klee {

  namespace util {
    struct ExprStructureHash  {
      unsigned operator()(const ref<Expr> e) const {
        return e->hash();
      }
    };
    
    struct ExprStructureCmp {
      bool operator()(const ref<Expr> &a, const ref<Expr> &b) const {
        return a==b;
      }
    };

    struct UpdateListStructureHash {
      unsigned operator()(const UpdateList &ul) const {
        return ul.hash();
      }
    };

    struct UpdateListStructureCmp {
      bool operator()(const UpdateList &a, const UpdateList &b) const {
        return a.compare(b);
      }
    };

    // For update list identity, we assume that update nodes are not shared
    // among arrays.

    struct UpdateListIdentityHash: public std::tr1::hash<uintptr_t> {
      unsigned operator()(const UpdateList &ul) const {
        return std::tr1::hash<uintptr_t>::operator ()((uintptr_t)ul.head);
      }
    };

    struct UpdateListIdentityCmp {
      bool operator()(const UpdateList &a, const UpdateList &b) const {
        return a.head == b.head;
      }
    };
  }
  
  template<class T> 
  class ExprHashMap : 
    public std::tr1::unordered_map<ref<Expr>,
           T,
           klee::util::ExprStructureHash,
           klee::util::ExprStructureCmp> {
  };
  
  typedef std::tr1::unordered_set<ref<Expr>,
          klee::util::ExprStructureHash,
          klee::util::ExprStructureCmp> ExprHashSet;

  template<class T>
  class UpdateListHashMap :
    public std::tr1::unordered_map<UpdateList,
           T,
           klee::util::UpdateListStructureHash,
           klee::util::UpdateListStructureCmp> {

    };

   typedef std::tr1::unordered_set<UpdateList,
           klee::util::UpdateListStructureHash,
           klee::util::UpdateListStructureCmp> UpdateListHashSet;
}

#endif
