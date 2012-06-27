//===-- InstructionInfoTable.h ----------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_LIB_INSTRUCTIONINFOTABLE_H
#define KLEE_LIB_INSTRUCTIONINFOTABLE_H

#include <map>
#include <string>
#include <set>
#include <ostream>
#include <tr1/unordered_map>

namespace llvm {
  class Function;
  class Instruction;
  class Module; 
}

namespace klee {
  /* Stores debug information for a KInstruction */
  struct InstructionInfo {
    unsigned id;
    const std::string &file;
    int file_id;
    unsigned line;
    unsigned assemblyLine;

  public:
    InstructionInfo(unsigned _id,
                    const std::string &_file,
                    int _file_id,
                    unsigned _line,
                    unsigned _assemblyLine)
      : id(_id), 
        file(_file),
        file_id(_file_id),
        line(_line),
        assemblyLine(_assemblyLine) {
    }
  };

  struct ltstr {
    bool operator()(const std::string *a, const std::string *b) const {
      return *a<*b;
    }
  };

  typedef std::tr1::unordered_map<const llvm::Instruction*, InstructionInfo> InfoMap;
  typedef std::map<const std::string*, int, ltstr> StringTable;

  class InstructionInfoTable {
    struct ltfunc {
      bool operator()(const llvm::Function *f1, const llvm::Function *f2) const;
    };

    std::string dummyString;
    InstructionInfo dummyInfo;
    InfoMap infos;
  private:
    int idCounter;
    const std::string *internString(std::string s, int &ID);
    bool getInstructionDebugInfo(const llvm::Instruction *I,
                                 const std::string *&File, int &FileID,
                                 unsigned &Line);

  public:
    InstructionInfoTable(llvm::Module *m);
    ~InstructionInfoTable();

    unsigned getMaxID() const;
    const InstructionInfo &getInfo(const llvm::Instruction*) const;
    const InstructionInfo &getFunctionInfo(const llvm::Function*) const;

    StringTable stringTable;
  };

}

#endif
