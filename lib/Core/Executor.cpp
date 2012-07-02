//===-- Executor.cpp ------------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "klee/Executor.h"

#include "ExternalDispatcher.h"
#include "ImpliedValue.h"
#include "Memory.h"
#include "MemoryManager.h"
#include "PTree.h"
#include "SpecialFunctionHandler.h"
#include "StatsTracker.h"
#include "TimingSolver.h"
#include "UserSearcher.h"
#include "klee/CoreStats.h"
#include "klee/LoggingSolvers.h"
#include "klee/data/ExprSerializer.h"
#include "klee/Internal/Module/InstructionInfoTable.h"
#include "klee/util/ExprPPrinter.h"

#include "llvm/Support/CommandLine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include "llvm/Target/TargetData.h"

#include <glog/logging.h>

#include <iomanip>
#include <string>
#include <sstream>


using namespace klee;

namespace {
 
  cl::opt<bool>
  NoPreferCex("no-prefer-cex",
              cl::init(false));

  cl::opt<bool>
  DebugPrintInstructions("debug-print-instructions",
                         cl::desc("Print instructions during execution."));

  cl::opt<bool>
  DebugCheckForImpliedValues("debug-check-for-implied-values");

  cl::opt<bool>
  DebugValidateSolver("debug-validate-solver",
          cl::init(false));

  cl::opt<bool>
  UseFastCexSolver("use-fast-cex-solver",
       cl::init(false));

  cl::opt<bool>
  UseIndependentSolver("use-independent-solver",
                       cl::init(true),
           cl::desc("Use constraint independence"));

  cl::opt<bool>
  UseCexCache("use-cex-cache",
              cl::init(true),
        cl::desc("Use counterexample caching"));

  cl::opt<bool>
  UseQueryPCLog("use-query-pc-log",
                cl::init(false));
  
  cl::opt<bool>
  UseSTPQueryPCLog("use-stp-query-pc-log",
                   cl::init(false));

  cl::opt<bool>
  UseCache("use-cache",
     cl::init(true),
     cl::desc("Use validity caching"));
  
  cl::opt<double>
  MaxInstructionTime("max-instruction-time",
                     cl::desc("Only allow a single instruction to take this much time (default=0 (off))"),
                     cl::init(0));

  cl::opt<double>
  MaxSTPTime("max-stp-time",
             cl::desc("Maximum amount of time for a single query (default=120s)"),
             cl::init(120.0));
  
  cl::opt<unsigned int>
  StopAfterNInstructions("stop-after-n-instructions",
                         cl::desc("Stop execution after specified number of instructions (0=off)"),
                         cl::init(0));

  cl::opt<bool>
  UseForkedSTP("use-forked-stp",
                 cl::desc("Run STP in forked process"),
                 cl::init(false));

  cl::opt<bool>
  STPOptimizeDivides("stp-optimize-divides", 
                 cl::desc("Optimize constant divides into add/shift/multiplies before passing to STP"),
                 cl::init(true));

  cl::opt<bool>
  UseBTORSolver("use-btor",
                cl::desc("Use BTOR solver"),
                cl::init(false));
}

//namespace klee {

Executor::Executor(const InterpreterOptions &opts,
                   InterpreterHandler *ih) 
  : Interpreter(opts),
    kmodule(0),
    interpreterHandler(ih),
    searcher(0),
    externalDispatcher(new ExternalDispatcher()),
    statsTracker(0),
    exprRecorder(0),
    pathWriter(0),
    symPathWriter(0),
    specialFunctionHandler(0),
    processTree(0),
    activeState(0),
    atMemoryLimit(false),
    inhibitForking(false),
    haltExecution(false),
    ivcEnabled(false),
    stpTimeout(MaxSTPTime != 0 && MaxInstructionTime != 0
         ? std::min(MaxSTPTime,MaxInstructionTime)
         : std::max(MaxSTPTime,MaxInstructionTime)),
    instrTime(MaxInstructionTime) {

  STPSolver *stpSolver;
  Solver *baseSolver;


	stpSolver = new STPSolver(UseForkedSTP, STPOptimizeDivides);
	baseSolver = stpSolver;


  Solver *solver = baseSolver;

  if (UseSTPQueryPCLog)
    solver = createPCLoggingSolver(
        solver,
        interpreterHandler->getOutputFilename("stp-queries.qlog"));

 
	if (UseFastCexSolver)
		solver = createFastCexSolver(solver);

	if (UseCexCache)
		solver = createCexCachingSolver(solver);

	if (UseCache)
		solver = createCachingSolver(solver);

	if (UseIndependentSolver)
		solver = createIndependentSolver(solver);

  if (DebugValidateSolver)
    solver = createValidatingSolver(solver, baseSolver);

  if (UseQueryPCLog)
    solver = createPCLoggingSolver(
        solver,
        interpreterHandler->getOutputFilename("queries.pc"));

  // TODO: Leaky
  exprRecorder = new ExprSerializer(*interpreterHandler->openOutputFile("expr.log"));


	this->solver = new TimingSolver(solver, stpSolver, statsTracker);

  memory = new MemoryManager();

  PrintDumpOnErrorSignal();
}


const Module *Executor::setModule(llvm::Module *module, 
                                  const ModuleOptions &opts) {
  assert(!kmodule && module && "can only register one module"); // XXX gross
  
  kmodule = new KModule(module);

  // Initialize the context.
  TargetData *TD = kmodule->targetData;
  Context::initialize(TD->isLittleEndian(),
                      (Expr::Width) TD->getPointerSizeInBits());

  specialFunctionHandler = new SpecialFunctionHandler(*this);

  specialFunctionHandler->prepare();
  kmodule->prepare(opts, interpreterHandler);
  specialFunctionHandler->bind();

  if (StatsTracker::useStatistics()) {
    statsTracker = 
      new StatsTracker(*this,
                       interpreterHandler->getOutputFilename("assembly.ll"),
                       true);
    solver->statsTracker = statsTracker;
  }
  
  return module;
}

Executor::~Executor() {
  delete memory;
  delete externalDispatcher;
  if (processTree)
    delete processTree;
  if (specialFunctionHandler)
    delete specialFunctionHandler;
  if (statsTracker)
    delete statsTracker;
  delete solver;
  delete kmodule;
}

/***/


void Executor::stepInstruction(ExecutionState &state) {
  if (DebugPrintInstructions) {
    printFileLine(state, state.pc());
    std::cerr << std::setw(10) << stats::instructions << " ";
    llvm::errs() << *(state.pc()->inst);
  }

  if (statsTracker)
    statsTracker->stepInstruction(state);

  ++state.totalInstructions;
  ++stats::instructions;
  state.prevPC() = state.pc();
  ++state.pc();

  if (stats::instructions==StopAfterNInstructions)
    haltExecution = true;
}

void Executor::printFileLine(ExecutionState &state, KInstruction *ki) {
  const InstructionInfo &ii = *ki->info;
  if (ii.file != "") 
    std::cerr << "     " << ii.file << ":" << ii.line << ":";
  else
    std::cerr << "     [no debug info]:";
}

std::string Executor::getAddressInfo(ExecutionState &state, 
                                     ref<Expr> address) const{
  std::ostringstream info;
  info << "\taddress: " << address << "\n";
  uint64_t example;
  if (ConstantExpr *CE = dyn_cast<ConstantExpr>(address)) {
    example = CE->getZExtValue();
  } else {
    ref<ConstantExpr> value;
    bool success = solver->getValue(data::OTHER, state, address, value);
    assert(success && "FIXME: Unhandled solver failure");
    (void) success;
    example = value->getZExtValue();
    info << "\texample: " << example << "\n";
    std::pair< ref<Expr>, ref<Expr> > res = solver->getRange(data::OTHER, state, address);
    info << "\trange: [" << res.first << ", " << res.second <<"]\n";
  }
  
  MemoryObject hack((unsigned) example);    
  MemoryMap::iterator lower = state.addressSpace().objects.upper_bound(&hack);
  info << "\tnext: ";
  if (lower==state.addressSpace().objects.end()) {
    info << "none\n";
  } else {
    const MemoryObject *mo = lower->first;
    std::string alloc_info;
    mo->getAllocInfo(alloc_info);
    info << "object at " << mo->address
         << " of size " << mo->size << "\n"
         << "\t\t" << alloc_info << "\n";
  }
  if (lower!=state.addressSpace().objects.begin()) {
    --lower;
    info << "\tprev: ";
    if (lower==state.addressSpace().objects.end()) {
      info << "none\n";
    } else {
      const MemoryObject *mo = lower->first;
      std::string alloc_info;
      mo->getAllocInfo(alloc_info);
      info << "object at " << mo->address 
           << " of size " << mo->size << "\n"
           << "\t\t" << alloc_info << "\n";
    }
  }

  return info.str();
}

Searcher *Executor::initSearcher(Searcher *base) {
  return constructUserSearcher(*this, base);
}

void Executor::runFunctionAsMain(Function *f, int argc, char **argv,
    char **envp) {

  ExecutionState *state = createRootState(f);
  initRootState(state, argc, argv, envp);

  run(*state);

  destroyStates();

}

unsigned Executor::getPathStreamID(const ExecutionState &state) {
  assert(pathWriter);
  return state.pathOS.getID();
}

unsigned Executor::getSymbolicPathStreamID(const ExecutionState &state) {
  assert(symPathWriter);
  return state.symPathOS.getID();
}

void Executor::getConstraintLog(const ExecutionState &state,
                                std::string &res,
                                bool asCVC) {
#if 0
  if (asCVC) {
    Query query(state.constraints(), ConstantExpr::alloc(0, Expr::Bool));
    char *log = solver->stpSolver->getConstraintLog(query);
    res = std::string(log);
    free(log);
  } else {
    std::ostringstream info;
    ExprPPrinter::printConstraints(info, state.constraints());
    res = info.str();    
  }
#endif
}

bool Executor::getSymbolicSolution(const ExecutionState &state,
                                   std::vector< 
                                   std::pair<std::string,
                                   std::vector<unsigned char> > >
                                   &res) {
  solver->setTimeout(stpTimeout);

  ExecutionState tmp(state);
  if (!NoPreferCex) {
    for (unsigned i = 0; i != state.symbolics.size(); ++i) {
      const MemoryObject *mo = state.symbolics[i].first;
      std::vector< ref<Expr> >::const_iterator pi = 
        mo->cexPreferences.begin(), pie = mo->cexPreferences.end();
      for (; pi != pie; ++pi) {
        bool mustBeTrue;
        bool success = solver->mustBeTrue(data::TEST_CASE_GENERATION, tmp, Expr::createIsZero(*pi),
                                          mustBeTrue);
        if (!success) break;
        if (!mustBeTrue) tmp.addConstraint(*pi);
      }
      if (pi!=pie) break;
    }
  }

  std::vector< std::vector<unsigned char> > values;
  std::vector<const Array*> objects;
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    objects.push_back(state.symbolics[i].second);
  bool success = solver->getInitialValues(data::TEST_CASE_GENERATION, tmp, objects, values);
  solver->setTimeout(0);
  if (!success) {
    LOG(WARNING) << "Unable to compute initial values (invalid constraints?)!";
    ExprPPrinter::printQuery(std::cerr,
                             state.constraints(),
                             ConstantExpr::alloc(0, Expr::Bool),
                             std::string(), std::string());
    return false;
  }
  
  for (unsigned i = 0; i != state.symbolics.size(); ++i)
    res.push_back(std::make_pair(state.symbolics[i].first->name, values[i]));
  return true;
}

void Executor::doImpliedValueConcretization(ExecutionState &state,
                                            ref<Expr> e,
                                            ref<ConstantExpr> value) {
  abort(); // FIXME: Broken until we sort out how to do the write back.

  if (DebugCheckForImpliedValues)
    ImpliedValue::checkForImpliedValues(solver->solver, e, value);

  ImpliedValueList results;
  ImpliedValue::getImpliedValues(e, value, results);
  for (ImpliedValueList::iterator it = results.begin(), ie = results.end();
       it != ie; ++it) {
    ReadExpr *re = it->first.get();
    
    if (ConstantExpr *CE = dyn_cast<ConstantExpr>(re->index)) {
      // FIXME: This is the sole remaining usage of the Array object
      // variable. Kill me.
      const MemoryObject *mo = 0; //re->updates.root->object;
      const ObjectState *os = state.addressSpace().findObject(mo);

      if (!os) {
        // object has been free'd, no need to concretize (although as
        // in other cases we would like to concretize the outstanding
        // reads, but we have no facility for that yet)
      } else {
        assert(!os->readOnly && 
               "not possible? read only object with static read?");
        ObjectState *wos = state.addressSpace().getWriteable(mo, os);
        wos->write(CE, it->second);
      }
    }
  }
}

static void PrintExecutorDump(void *cookie) {
  Executor *executor = (Executor*)cookie;
  executor->PrintDump(std::cerr);
}

void Executor::PrintDump(std::ostream &os) {
  if (activeState) {
    // Print state stack trace
    os << "State trace:" << std::endl;
    activeState->getStackTrace().dump(os);

    // Print memory map
    os << "Memory map:" << std::endl;
    activeState->addressSpace().DumpContents(os);
  } else {
    os << "No active state set" << std::endl;
  }
}

void Executor::PrintDumpOnErrorSignal() {
  llvm::sys::AddSignalHandler(PrintExecutorDump, (void*)this);
}

///

Interpreter *Interpreter::create(const InterpreterOptions &opts,
                                 InterpreterHandler *ih) {
  return new Executor(opts, ih);
}

//}
