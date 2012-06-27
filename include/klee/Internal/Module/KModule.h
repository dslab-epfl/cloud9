//===-- KModule.h -----------------------------------------------*- C++ -*-===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef KLEE_KMODULE_H
#define KLEE_KMODULE_H

#include "klee/Interpreter.h"
#include "llvm/ADT/StringRef.h"

#include <map>
#include <set>
#include <vector>
#include <istream>

namespace llvm {
  class BasicBlock;
  class Constant;
  class Function;
  class Instruction;
  class Module;
  class TargetData;
}

namespace klee {
  struct Cell;
  class Executor;
  class Expr;
  class InterpreterHandler;
  class InstructionInfoTable;
  struct KInstruction;
  class KModule;
  template<class T> class ref;

  namespace data {
    class DebugInfo;
  }

  struct KFunction {
    llvm::Function *function;
    int nameID;

    unsigned numArgs, numRegisters;

    unsigned numInstructions;
    KInstruction **instructions;
    KInstruction **instrPostOrder;

    std::map<llvm::BasicBlock*, unsigned> basicBlockEntry;
  private:
    KFunction(const KFunction&);
    KFunction &operator=(const KFunction&);

  public:
    explicit KFunction(llvm::Function*, KModule *);
    ~KFunction();

    unsigned getArgRegister(unsigned index) { return index; }
  };


  class KConstant {
  public:
    /// Actual LLVM constant this represents.
    llvm::Constant* ct;

    /// The constant ID.
    unsigned id;

    /// First instruction where this constant was encountered, or NULL
    /// if not applicable/unavailable.
    KInstruction *ki;

    KConstant(llvm::Constant*, unsigned, KInstruction*);
  };

  typedef std::map<const llvm::Function*, KFunction*> FunctionMap;


  class KModule {
  public:
    llvm::Module *module;
    llvm::TargetData *targetData;
    
    // Some useful functions to know the address of
    llvm::Function *dbgStopPointFn, *kleeMergeFn;

    // Our shadow versions of LLVM structures.
    std::vector<KFunction*> functions;
    FunctionMap functionMap;

    // Functions which escape (may be called indirectly)
    // XXX change to KFunction
    std::set<llvm::Function*> escapingFunctions;

    // A name->id mapping of symbol names
    std::map<llvm::StringRef, int> nameTable;

    InstructionInfoTable *infos;

    std::vector<llvm::Constant*> constants;
    std::map<llvm::Constant*, KConstant*> constantMap;
    KConstant* getKConstant(llvm::Constant *c);

    Cell *constantTable;

  private:
    typedef std::pair<std::string, int> program_point_t;
    typedef std::map<std::string, std::set<program_point_t> > vpoints_t;

    vpoints_t   vulnerablePoints;

    void readVulnerablePoints(std::istream &is);
    bool isVulnerablePoint(KInstruction *kinst);

  public:
    KModule(llvm::Module *_module);
    ~KModule();

    /// Initialize local data structures.
    //
    // FIXME: ihandler should not be here
    void prepare(const Interpreter::ModuleOptions &opts, 
                 InterpreterHandler *ihandler);

    /// Return an id for the given constant, creating a new one if necessary.
    unsigned getConstantID(llvm::Constant *c, KInstruction* ki);

    void fillInstructionDebugInfo(const llvm::Instruction *i,
                                  data::DebugInfo &debug_info) const;
    void fillFunctionDebugInfo(const llvm::Function *f,
                               data::DebugInfo &debug_info) const;

    void writeDebugTable(std::ostream &os) const;
  };
} // End klee namespace

#endif
