//===-- KModule.cpp -------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Internal/Module/KModule.h"

#include "Passes.h"

#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Internal/Module/Cell.h"
#include "klee/Internal/Module/KInstruction.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/Internal/Support/ModuleUtil.h"

#include "klee/data/DebugInfo.pb.h"
#include "klee/data/Support.h"

#include "llvm/BasicBlock.h"
#include "llvm/Bitcode/ReaderWriter.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Module.h"
#include "llvm/PassManager.h"
#include "llvm/ValueSymbolTable.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/raw_os_ostream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/CFG.h"
#include "llvm/Target/TargetData.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/PostOrderIterator.h"

#include <glog/logging.h>

#include <sstream>
#include <fstream>
#include <string>
#include <cstdlib>

using namespace llvm;
using namespace klee;

using llvm::sys::Path;

namespace {
  enum SwitchImplType {
    eSwitchTypeSimple,
    eSwitchTypeLLVM,
    eSwitchTypeInternal
  };

  cl::list<std::string>
  MergeAtExit("merge-at-exit");
    
  cl::opt<bool>
  NoTruncateSourceLines("no-truncate-source-lines",
                        cl::desc("Don't truncate long lines in the output source"));

  cl::opt<bool>
  OutputSource("output-source",
               cl::desc("Write the assembly for the final transformed source"),
               cl::init(true));

  cl::opt<bool>
  OutputModule("output-module",
               cl::desc("Write the bitcode for the final transformed module"),
               cl::init(false));

  cl::opt<bool>
  OutputDebugTable("output-debug-table",
                   cl::desc("Write the table that allows the recovery of "
                            "function and file names in protobuf logs"),
                   cl::init(true));

  cl::opt<SwitchImplType>
  SwitchType("switch-type", cl::desc("Select the implementation of switch"),
             cl::values(clEnumValN(eSwitchTypeSimple, "simple", 
                                   "lower to ordered branches"),
                        clEnumValN(eSwitchTypeLLVM, "llvm", 
                                   "lower using LLVM"),
                        clEnumValN(eSwitchTypeInternal, "internal", 
                                   "execute switch internally"),
                        clEnumValEnd),
             cl::init(eSwitchTypeInternal));
  
  cl::opt<bool>
  DebugPrintEscapingFunctions("debug-print-escaping-functions",
                              cl::desc("Print functions whose address is taken."));

  cl::opt<bool>
  DebugPrintInstructionCount("debug-print-instruction-count",
                             cl::desc("Print per-function instruction count."));

  cl::opt<std::string>
  VulnerableSites("vulnerable-sites",
      cl::desc("A file describing vulnerable sites in the tested program"));
}

void KModule::readVulnerablePoints(std::istream &is) {
  std::string fnName;
  std::string callSite;

  while (!is.eof()) {
    is >> fnName >> callSite;

    if (is.eof() && (fnName.length() == 0 || callSite.length() == 0))
      break;

    size_t splitPoint = callSite.find(':');
    assert(splitPoint != std::string::npos);

    std::string fileName = callSite.substr(0, splitPoint);
    std::string lineNoStr = callSite.substr(splitPoint+1);
    unsigned lineNo = atoi(lineNoStr.c_str());

    if (lineNo == 0) {
      // Skipping this
      continue;

    }

    vulnerablePoints[fnName].insert(std::make_pair(fileName, lineNo));
  }
}

bool KModule::isVulnerablePoint(KInstruction *kinst) {
  Function *target;

  if (CallInst *inst = dyn_cast<CallInst>(kinst->inst)) {
    target = inst->getCalledFunction();
  } else if (InvokeInst *inst = dyn_cast<InvokeInst>(kinst->inst)) {
    target = inst->getCalledFunction();
  } else {
    assert(false);
  }

  if (!target)
    return false;

  std::string targetName = target->getName();

  if (targetName.find("__klee_model_") == 0)
    targetName = targetName.substr(strlen("__klee_model_"));

  if (vulnerablePoints.count(target->getName()) == 0)
    return false;

  Path sourceFile(kinst->info->file);
  program_point_t cpoint = std::make_pair(llvm::sys::path::filename(StringRef(sourceFile.str())), kinst->info->line);

  if (vulnerablePoints[target->getName()].count(cpoint) == 0)
    return false;

  return true;
}

KModule::KModule(Module *_module) 
  : module(_module),
    targetData(new TargetData(module)),
    dbgStopPointFn(0),
    kleeMergeFn(0),
    infos(0),
    constantTable(0) {

}

KModule::~KModule() {
  delete[] constantTable;
  delete infos;

  for (std::vector<KFunction*>::iterator it = functions.begin(), 
         ie = functions.end(); it != ie; ++it)
    delete *it;

  delete targetData;
  delete module;
}

void KModule::fillInstructionDebugInfo(const llvm::Instruction *i,
                                       data::DebugInfo &debug_info) const {
  const InstructionInfo &info = infos->getInfo(i);

  debug_info.set_file_id(info.file_id);
  debug_info.set_line_number(info.line);
  debug_info.set_assembly_number(info.assemblyLine);

}

void KModule::fillFunctionDebugInfo(const llvm::Function *f,
                                    data::DebugInfo &debug_info) const {
  const InstructionInfo &info = infos->getFunctionInfo(f);

  FunctionMap::const_iterator it = functionMap.find(f);
  if (it != functionMap.end()) {
    debug_info.set_function_id(it->second->nameID);
  }

  debug_info.set_file_id(info.file_id);
  debug_info.set_line_number(info.line);
  debug_info.set_assembly_number(info.assemblyLine);
}

void KModule::writeDebugTable(std::ostream &os) const {
  data::DebugTable debug_table;

  data::StringTable *file_table = debug_table.mutable_file_table();

  for (StringTable::iterator it = infos->stringTable.begin(),
      ie = infos->stringTable.end(); it != ie; ++it) {
    data::StringTable_StringTableEntry *entry = file_table->add_entry();
    entry->set_value(*(it->first));
    entry->set_id(it->second);
  }

  data::StringTable *function_table = debug_table.mutable_function_table();

  for (std::map<llvm::StringRef, int>::const_iterator it = nameTable.begin(),
      ie = nameTable.end(); it != ie; ++it) {
    data::StringTable_StringTableEntry *entry = function_table->add_entry();
    entry->set_value(it->first.str());
    entry->set_id(it->second);
  }

  WriteProtoMessage(debug_table, os, false);
}

/***/

namespace llvm {
extern void Optimize(Module*);
}

// what a hack
static Function *getStubFunctionForCtorList(Module *m,
                                            GlobalVariable *gv, 
                                            std::string name) {
  assert(!gv->isDeclaration() && !gv->hasInternalLinkage() &&
         "do not support old LLVM style constructor/destructor lists");
  
  std::vector<Type*> nullary;

  Function *fn = Function::Create(
      FunctionType::get(Type::getVoidTy(getGlobalContext()),
      ArrayRef<Type*>(nullary), false),
      GlobalVariable::InternalLinkage,
      name, m);
  BasicBlock *bb = BasicBlock::Create(getGlobalContext(), "entry", fn);
  
  // From lli:
  // Should be an array of '{ int, void ()* }' structs.  The first value is
  // the init priority, which we ignore.
  ConstantArray *arr = dyn_cast<ConstantArray>(gv->getInitializer());
  if (arr) {
    for (unsigned i=0; i<arr->getNumOperands(); i++) {
      ConstantStruct *cs = cast<ConstantStruct>(arr->getOperand(i));
      assert(cs->getNumOperands()==2 && "unexpected element in ctor initializer list");
      
      Constant *fp = cs->getOperand(1);      
      if (!fp->isNullValue()) {
        if (llvm::ConstantExpr *ce = dyn_cast<llvm::ConstantExpr>(fp))
          fp = ce->getOperand(0);

        if (Function *f = dyn_cast<Function>(fp)) {
    CallInst::Create(f, "", bb);
        } else {
          assert(0 && "unable to get function pointer from ctor initializer list");
        }
      }
    }
  }
  
  ReturnInst::Create(getGlobalContext(), bb);

  return fn;
}

static void injectStaticConstructorsAndDestructors(Module *m) {
  GlobalVariable *ctors = m->getNamedGlobal("llvm.global_ctors");
  GlobalVariable *dtors = m->getNamedGlobal("llvm.global_dtors");
  
  if (ctors || dtors) {
    Function *mainFn = m->getFunction("main");
    assert(mainFn && "unable to find main function");

    if (ctors)
    CallInst::Create(getStubFunctionForCtorList(m, ctors, "klee.ctor_stub"),
         "", mainFn->begin()->begin());
    if (dtors) {
      Function *dtorStub = getStubFunctionForCtorList(m, dtors, "klee.dtor_stub");
      for (Function::iterator it = mainFn->begin(), ie = mainFn->end();
           it != ie; ++it) {
        if (isa<ReturnInst>(it->getTerminator()))
    CallInst::Create(dtorStub, "", it->getTerminator());
      }
    }
  }
}

static void forceImport(Module *m, const char *name, Type *retType, ...) {
  // If module lacks an externally visible symbol for the name then we
  // need to create one. We have to look in the symbol table because
  // we want to check everything (global variables, functions, and
  // aliases).

  Value *v = m->getValueSymbolTable().lookup(name);
  GlobalValue *gv = dyn_cast_or_null<GlobalValue>(v);

  if (!gv || gv->hasInternalLinkage()) {
    va_list ap;

    va_start(ap, retType);
    std::vector<Type *> argTypes;
    while (Type *t = va_arg(ap, Type*))
      argTypes.push_back(t);
    va_end(ap);

    m->getOrInsertFunction(name, FunctionType::get(retType, ArrayRef<Type*>(argTypes), false));
  }
}

void KModule::prepare(const Interpreter::ModuleOptions &opts,
                      InterpreterHandler *ih) {
  if (!MergeAtExit.empty()) {
    Function *mergeFn = module->getFunction("klee_merge");
    if (!mergeFn) {
      llvm::FunctionType *Ty =
        FunctionType::get(Type::getVoidTy(getGlobalContext()), 
                          ArrayRef<Type*>(), false);
      mergeFn = Function::Create(Ty, GlobalVariable::ExternalLinkage,
         "klee_merge",
         module);
    }

    for (cl::list<std::string>::iterator it = MergeAtExit.begin(), 
           ie = MergeAtExit.end(); it != ie; ++it) {
      std::string &name = *it;
      Function *f = module->getFunction(name);
      if (!f) {
        LOG(FATAL) << "cannot insert merge-at-exit for: "
            << name.c_str() << " (cannot find)";
      } else if (f->isDeclaration()) {
        LOG(FATAL) << "cannot insert merge-at-exit for: "
            << name.c_str() << " (external)";
      }

      BasicBlock *exit = BasicBlock::Create(getGlobalContext(), "exit", f);
      PHINode *result = 0;
      if (f->getReturnType() != Type::getVoidTy(getGlobalContext()))
        result = PHINode::Create(f->getReturnType(), 0, "retval", exit);
      CallInst::Create(mergeFn, "", exit);
      ReturnInst::Create(getGlobalContext(), result, exit);

      LOG(INFO) << "Adding klee_merge at exit of: " << name;
      for (llvm::Function::iterator bbit = f->begin(), bbie = f->end(); 
           bbit != bbie; ++bbit) {
        if (&*bbit != exit) {
          Instruction *i = bbit->getTerminator();
          if (i->getOpcode()==Instruction::Ret) {
            if (result) {
              result->addIncoming(i->getOperand(0), bbit);
            }
            i->eraseFromParent();
      BranchInst::Create(exit, bbit);
          }
        }
      }
    }
  }

  if (VulnerableSites != "") {
    std::ifstream fs(VulnerableSites.c_str());
    assert(!fs.fail());

    readVulnerablePoints(fs);
  }

  // Inject checks prior to optimization... we also perform the
  // invariant transformations that we will end up doing later so that
  // optimize is seeing what is as close as possible to the final
  // module.
  PassManager pm;
  pm.add(createLowerAtomicPass());
  pm.add(new RaiseAsmPass());
  if (opts.CheckDivZero) pm.add(new DivCheckPass());
  pm.add(new LowerSSEPass());
  // FIXME: This false here is to work around a bug in
  // IntrinsicLowering which caches values which may eventually be
  // deleted (via RAUW). This can be removed once LLVM fixes this
  // issue.
  pm.add(new IntrinsicCleanerPass(*targetData, false));
  pm.add(new ThrowCleanerPass());
  pm.add(createPruneEHPass());
  pm.add(createInstructionCombiningPass());
  pm.add(createCFGSimplificationPass());
  pm.add(createAggressiveDCEPass());
  pm.add(createGlobalDCEPass());
  pm.run(*module);

  if (opts.Optimize)
    Optimize(module);

  // Force importing functions required by intrinsic lowering. Kind of
  // unfortunate clutter when we don't need them but we won't know
  // that until after all linking and intrinsic lowering is
  // done. After linking and passes we just try to manually trim these
  // by name. We only add them if such a function doesn't exist to
  // avoid creating stale uses.

  llvm::Type *i8Ty = Type::getInt8Ty(getGlobalContext());
  forceImport(module, "memcpy", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
  forceImport(module, "memmove", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
  forceImport(module, "memset", PointerType::getUnqual(i8Ty),
              PointerType::getUnqual(i8Ty),
              Type::getInt32Ty(getGlobalContext()),
              targetData->getIntPtrType(getGlobalContext()), (Type*) 0);
  forceImport(module, "uadd", StructType::get(Type::getInt32Ty(getGlobalContext()),
                                              Type::getInt1Ty(getGlobalContext()),
                                              NULL),
                              Type::getInt32Ty(getGlobalContext()),
                              Type::getInt32Ty(getGlobalContext()),
                              (Type*)0);

  // FIXME: Missing force import for various math functions.

  // FIXME: Find a way that we can test programs without requiring
  // this to be linked in, it makes low level debugging much more
  // annoying.
  llvm::sys::Path path(opts.LibraryDir);
  path.appendComponent("libkleeRuntimeIntrinsic.a");
  module = linkWithLibrary(module, path.c_str());

  // Needs to happen after linking (since ctors/dtors can be modified)
  // and optimization (since global optimization can rewrite lists).
  injectStaticConstructorsAndDestructors(module);

  // Finally, run the passes that maintain invariants we expect during
  // interpretation. We run the intrinsic cleaner just in case we
  // linked in something with intrinsics but any external calls are
  // going to be unresolved. We really need to handle the intrinsics
  // directly I think?
  PassManager pm3;
  pm3.add(createCFGSimplificationPass());
  switch(SwitchType) {
  case eSwitchTypeInternal: break;
  case eSwitchTypeSimple: pm3.add(new LowerSwitchPass()); break;
  case eSwitchTypeLLVM:  pm3.add(createLowerSwitchPass()); break;
  default: LOG(FATAL) << "Invalid --switch-type";
  }
  pm3.add(new IntrinsicCleanerPass(*targetData));
  pm3.add(new PhiCleanerPass());
  pm3.run(*module);

  // For cleanliness see if we can discard any of the functions we
  // forced to import.
  Function *f;
  f = module->getFunction("memcpy");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memmove");
  if (f && f->use_empty()) f->eraseFromParent();
  f = module->getFunction("memset");
  if (f && f->use_empty()) f->eraseFromParent();

  // Write out the .ll assembly file. We truncate long lines to work
  // around a kcachegrind parsing bug (it puts them on new lines), so
  // that source browsing works.
  if (OutputSource) {
    std::ostream *os = ih->openOutputFile("assembly.ll");
    assert(os && os->good() && "unable to open source output");

    llvm::raw_os_ostream *ros = new llvm::raw_os_ostream(*os);

    // We have an option for this in case the user wants a .ll they
    // can compile.
    if (NoTruncateSourceLines) {
      *ros << *module;
    } else {
      bool truncated = false;
      std::string string;
      llvm::raw_string_ostream rss(string);
      rss << *module;
      rss.flush();
      const char *position = string.c_str();

      for (;;) {
        const char *end = index(position, '\n');
        if (!end) {
          *ros << position;
          break;
        } else {
          unsigned count = (end - position) + 1;
          if (count<255) {
            ros->write(position, count);
          } else {
            ros->write(position, 254);
            *ros << "\n";
            truncated = true;
          }
          position = end+1;
        }
      }
    }
    delete ros;

    delete os;
  }

  if (OutputModule) {
    std::ostream *f = ih->openOutputFile("final.bc");
    llvm::raw_os_ostream* rfs = new llvm::raw_os_ostream(*f);
    WriteBitcodeToFile(module, *rfs);
    delete rfs;
    delete f;
  }

  dbgStopPointFn = module->getFunction("llvm.dbg.stoppoint");
  kleeMergeFn = module->getFunction("klee_merge");

  /* Build shadow structures */

  infos = new InstructionInfoTable(module);

  std::map<std::string, Function*> fnList;
  
  for (Module::iterator it = module->begin(), ie = module->end();
         it != ie; ++it) {
    if (it->isDeclaration())
      continue;

    fnList[it->getName()] = it;
  }

  int nameCounter = 0;

  for (std::map<std::string, Function*>::iterator it = fnList.begin();
      it != fnList.end(); it++) {
    Function *fn = it->second;
    
    KFunction *kf = new KFunction(fn, this);
    kf->nameID = nameCounter;
    nameTable.insert(std::make_pair(fn->getName(), nameCounter++));

    if (DebugPrintInstructionCount) {
      LOG(INFO) << "Function '" << fn->getName().str() << "': "
                << kf->numInstructions << " instructions";
    }

    for (unsigned i=0; i<kf->numInstructions; ++i) {
      KInstruction *ki = kf->instructions[i];
      ki->info = &infos->getInfo(ki->inst);

      if (ki->inst->getOpcode() == Instruction::Call) {
        KCallInstruction* kCallI = dyn_cast<KCallInstruction>(ki);
        kCallI->vulnerable = isVulnerablePoint(ki);
      }

      Path sourceFile(ki->info->file);
      program_point_t pPoint = std::make_pair(llvm::sys::path::filename(StringRef(sourceFile.str())),
          ki->info->line);
    }

    functions.push_back(kf);
    functionMap.insert(std::make_pair(fn, kf));
  }

  /* Compute various interesting properties */

  for (std::vector<KFunction*>::iterator it = functions.begin(), 
         ie = functions.end(); it != ie; ++it) {
    KFunction *kf = *it;
    if (functionEscapes(kf->function))
      escapingFunctions.insert(kf->function);
  }

  if (DebugPrintEscapingFunctions && !escapingFunctions.empty()) {
    llvm::errs() << "KLEE: escaping functions: [";
    for (std::set<Function*>::iterator it = escapingFunctions.begin(), 
         ie = escapingFunctions.end(); it != ie; ++it) {
      llvm::errs() << (*it)->getName() << ", ";
    }
    llvm::errs() << "]\n";
  }

  if (OutputDebugTable) {
    std::ostream *f = ih->openOutputFile("strings.bin");
    writeDebugTable(*f);
    f->flush();
    delete f;
  }
}

KConstant* KModule::getKConstant(Constant *c) {
  std::map<llvm::Constant*, KConstant*>::iterator it = constantMap.find(c);
  if (it != constantMap.end())
    return it->second;
  return NULL;
}

unsigned KModule::getConstantID(Constant *c, KInstruction* ki) {
  KConstant *kc = getKConstant(c);
  if (kc)
    return kc->id;  

  unsigned id = constants.size();
  kc = new KConstant(c, id, ki);
  constantMap.insert(std::make_pair(c, kc));
  constants.push_back(c);
  return id;
}

/***/

KConstant::KConstant(llvm::Constant* _ct, unsigned _id, KInstruction* _ki) {
  ct = _ct;
  id = _id;
  ki = _ki;
}

/***/

static int getOperandNum(Value *v,
                         std::map<Instruction*, unsigned> &registerMap,
                         KModule *km,
                         KInstruction *ki) {
  if (Instruction *inst = dyn_cast<Instruction>(v)) {
    return registerMap[inst];
  } else if (Argument *a = dyn_cast<Argument>(v)) {
    return a->getArgNo();
  } else if (isa<BasicBlock>(v) || isa<InlineAsm>(v) ||
             isa<MDNode>(v)) {
    return -1;
  } else {
    assert(isa<Constant>(v));
    Constant *c = cast<Constant>(v);
    return -(km->getConstantID(c, ki) + 2);
  }
}

KFunction::KFunction(llvm::Function *_function,
                     KModule *km) 
  : function(_function),
    numArgs(function->arg_size()),
    numInstructions(0) {
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    BasicBlock *bb = bbit;
    basicBlockEntry[bb] = numInstructions;
    numInstructions += bb->size();
  }

  instructions = new KInstruction*[numInstructions];
  instrPostOrder = new KInstruction*[numInstructions];

  std::map<Instruction*, unsigned> registerMap;

  // The first arg_size() registers are reserved for formals.
  unsigned rnum = numArgs;
  for (llvm::Function::iterator bbit = function->begin(), 
         bbie = function->end(); bbit != bbie; ++bbit) {
    for (llvm::BasicBlock::iterator it = bbit->begin(), ie = bbit->end();
         it != ie; ++it)
      registerMap[it] = rnum++;
  }
  numRegisters = rnum;
  
  unsigned i = 0;

  for (llvm::Function::iterator bbit = function->begin(),
      bbie = function->end(); bbit != bbie; ++bbit) {
    for (BasicBlock::iterator it = bbit->begin(), ie = bbit->end(); it != ie;++it) {
      KInstruction *ki;
      Instruction *inst = it;

      switch(inst->getOpcode()) {
      case Instruction::GetElementPtr:
      case Instruction::InsertValue:
      case Instruction::ExtractValue:
        ki = new KGEPInstruction(); break;
      case Instruction::Call:
      case Instruction::Invoke:
        ki = new KCallInstruction();
        break;
      default:
        ki = new KInstruction(); break;
      }

      ki->inst = inst;
      ki->dest = registerMap[inst];

      if (isa<CallInst>(inst) || isa<InvokeInst>(inst)) {
        CallSite cs(inst);
        unsigned numArgs = cs.arg_size();
        ki->operands = new int[numArgs+1];
        ki->operands[0] = getOperandNum(cs.getCalledValue(), registerMap, km,
                                        ki);
        for (unsigned j=0; j<numArgs; j++) {
          Value *v = cs.getArgument(j);
          ki->operands[j+1] = getOperandNum(v, registerMap, km, ki);
        }
      }
      else {
        unsigned numOperands = inst->getNumOperands();
        ki->operands = new int[numOperands];
        for (unsigned j=0; j<numOperands; j++) {
          Value *v = inst->getOperand(j);
        
          if (Instruction *inst = dyn_cast<Instruction>(v)) {
            ki->operands[j] = registerMap[inst];
          } else if (Argument *a = dyn_cast<Argument>(v)) {
            ki->operands[j] = a->getArgNo();
          } else if (isa<BasicBlock>(v) || isa<InlineAsm>(v) ||
                      isa<MDNode>(v)) {
            ki->operands[j] = -1;
          } else {
            assert(isa<Constant>(v));
            Constant *c = cast<Constant>(v);
            ki->operands[j] = -(km->getConstantID(c, ki) + 2);
          }
        }
      }

      instructions[i++] = ki;
    }
  }

  // Compute the post-order traversal
  unsigned poIndex = 0;
  BasicBlock *entryBB = &function->getEntryBlock();
  for (po_iterator<BasicBlock*> poIt = po_begin(entryBB),
      poIe = po_end(entryBB); poIt != poIe; ++poIt) {
    BasicBlock *bb = *poIt;
    unsigned bbEntry = basicBlockEntry[bb];
    for (unsigned i = 0; i < bb->size(); ++i) {
      instrPostOrder[poIndex + i] = instructions[bbEntry + bb->size() - i - 1];
    }
    poIndex += bb->size();
  }
}

KFunction::~KFunction() {
  for (unsigned i=0; i<numInstructions; ++i)
    delete instructions[i];
  delete[] instructions;
  delete[] instrPostOrder;
}
