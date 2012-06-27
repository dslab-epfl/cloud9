/* -*- mode: c++; c-basic-offset: 2; -*- */

#include "klee/ExecutionState.h"
#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Statistics.h"
#include "klee/Config/config.h"
#include "klee/Internal/ADT/KTest.h"
#include "klee/Internal/ADT/TreeStream.h"
#include "klee/Internal/Support/ModuleUtil.h"
#include "klee/Internal/System/Time.h"
#include "klee/KleeHandler.h"
#include "klee/Init.h"

#include "cloud9/worker/KleeCommon.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "llvm/Constants.h"
#include "llvm/Module.h"
#include "llvm/Type.h"
#include "llvm/InstrTypes.h"
#include "llvm/Instruction.h"
#include "llvm/Instructions.h"
#include "llvm/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"

#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"
#include "llvm/Support/CallSite.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/InlineAsm.h"
#include <iostream>
#include <fstream>
#include <cerrno>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>

#include <iostream>
#include <iterator>
#include <fstream>
#include <sstream>

using namespace llvm;
using namespace klee;

#define CLOUD9_STATS_FILE_NAME    "c9-stats.txt"
#define CLOUD9_EVENTS_FILE_NAME   "c9-events.txt"

namespace {

  cl::opt<std::string>
  RunInDir("run-in", cl::desc("Change to the given directory prior to executing"));
    
  cl::opt<bool>
  OptimizeModule("optimize", 
                 cl::desc("Optimize before execution"));

  cl::opt<bool>
  CheckDivZero("check-div-zero", 
               cl::desc("Inject checks for division-by-zero"),
               cl::init(true));

  // this is a fake entry, its automagically handled
  cl::list<std::string>
  ReadArgsFilesFake("read-args", 
                    cl::desc("File to read arguments from (one arg per line)"));
  
  cl::opt<unsigned>
  MakeConcreteSymbolic("make-concrete-symbolic",
                       cl::desc("Rate at which to make concrete reads symbolic (0=off)"),
                       cl::init(0));

  cl::opt<bool>
  Watchdog("watchdog",
           cl::desc("Use a watchdog process to enforce --max-time."),
           cl::init(0));
}

extern cl::opt<double> MaxTime;

//===----------------------------------------------------------------------===//
// main Driver function
//
#if ENABLE_STPLOG == 1
extern "C" void STPLOG_init(const char *);
#endif

static std::string strip(std::string &in) {
    unsigned len = in.size();
    unsigned lead = 0, trail = len;
    while (lead < len && isspace(in[lead]))
        ++lead;
    while (trail > lead && isspace(in[trail - 1]))
        --trail;
    return in.substr(lead, trail - lead);
}

static void readArgumentsFromFile(char *file, std::vector<std::string> &results) {
  std::ifstream f(file);
  assert(f.is_open() && "unable to open input for reading arguments");
  while (!f.eof()) {
    std::string line;
    std::getline(f, line);
    line = strip(line);
    if (!line.empty())
      results.push_back(line);
  }
  f.close();
}

static void parseArguments(int argc, char **argv) {
  std::vector<std::string> arguments;

  for (int i=1; i<argc; i++) {
    if (!strcmp(argv[i],"--read-args") && i+1<argc) {
      readArgumentsFromFile(argv[++i], arguments);
    } else {
      arguments.push_back(argv[i]);
    }
  }
    
  int numArgs = arguments.size() + 1;
  char **argArray = new char*[numArgs+1];
  argArray[0] = argv[0];
  argArray[numArgs] = 0;
  for (int i=1; i<numArgs; i++) {
    argArray[i] = new char[arguments[i-1].size() + 1];
    std::copy(arguments[i-1].begin(), arguments[i-1].end(), argArray[i]);
    argArray[i][arguments[i-1].size()] = '\0';
  }

  cl::ParseCommandLineOptions(numArgs, (char**) argArray, " klee\n");
  for (int i=1; i<numArgs; i++) {
    delete[] argArray[i];
  }
  delete[] argArray;
}





static Interpreter *theInterpreter = 0;

static bool interrupted = false;

// Pulled out so it can be easily called from a debugger.
extern "C"
void halt_execution() {
  theInterpreter->setHaltExecution(true);
}

extern "C"
void stop_forking() {
  theInterpreter->setInhibitForking(true);
}

static void interrupt_handle() {
  if (!interrupted && theInterpreter) {
    std::cerr << "KLEE: ctrl-c detected, requesting interpreter to halt.\n";
    halt_execution();
    sys::SetInterruptFunction(interrupt_handle);
  } else {
    std::cerr << "KLEE: ctrl-c detected, exiting.\n";
    exit(1);
  }
  interrupted = true;
}

// This is a temporary hack. If the running process has access to
// externals then it can disable interrupts, which screws up the
// normal "nice" watchdog termination process. We try to request the
// interpreter to halt using this mechanism as a last resort to save
// the state data before going ahead and killing it.
static void halt_via_gdb(int pid) {
  char buffer[256];
  sprintf(buffer, 
          "gdb --batch --eval-command=\"p halt_execution()\" "
          "--eval-command=detach --pid=%d &> /dev/null",
          pid);
  //  fprintf(stderr, "KLEE: WATCHDOG: running: %s\n", buffer);
  if (system(buffer)==-1) 
    perror("system");
}

// returns the end of the string put in buf
static char *format_tdiff(char *buf, long seconds)
{
  assert(seconds >= 0);

  long minutes = seconds / 60;  seconds %= 60;
  long hours   = minutes / 60;  minutes %= 60;
  long days    = hours   / 24;  hours   %= 24;

  buf = strrchr(buf, '\0');
  if (days > 0) buf += sprintf(buf, "%ld days, ", days);
  buf += sprintf(buf, "%02ld:%02ld:%02ld", hours, minutes, seconds);
  return buf;
}

int main(int argc, char **argv, char **envp) {  
#if ENABLE_STPLOG == 1
  STPLOG_init("stplog.c");
#endif

  atexit(llvm_shutdown);  // Call llvm_shutdown() on exit.

  google::InitGoogleLogging(argv[0]);

  llvm::InitializeNativeTarget();

  parseArguments(argc, argv);
  sys::PrintStackTraceOnErrorSignal();

  if (Watchdog) {
    if (MaxTime==0) {
      LOG(FATAL) << "--watchdog used without --max-time";
    }

    int pid = fork();
    if (pid<0) {
      LOG(FATAL) << "Unable to fork watchdog";
    } else if (pid) {
      fprintf(stderr, "KLEE: WATCHDOG: watching %d\n", pid);
      fflush(stderr);

      double nextStep = util::getWallTime() + MaxTime*1.1;
      int level = 0;

      // Simple stupid code...
      while (1) {
        sleep(1);

        int status, res = waitpid(pid, &status, WNOHANG);

        if (res < 0) {
          if (errno==ECHILD) { // No child, no need to watch but
                               // return error since we didn't catch
                               // the exit.
            fprintf(stderr, "KLEE: watchdog exiting (no child)\n");
            return 1;
          } else if (errno!=EINTR) {
            perror("watchdog waitpid");
            exit(1);
          }
        } else if (res==pid && WIFEXITED(status)) {
          return WEXITSTATUS(status);
        } else {
          double time = util::getWallTime();

          if (time > nextStep) {
            ++level;
            
            if (level==1) {
              fprintf(stderr, "KLEE: WATCHDOG: time expired, attempting halt via INT\n");
              kill(pid, SIGINT);
            } else if (level==2) {
              fprintf(stderr, "KLEE: WATCHDOG: time expired, attempting halt via gdb\n");
              halt_via_gdb(pid);
            } else {
              fprintf(stderr, "KLEE: WATCHDOG: kill(9)ing child (I tried to be nice)\n");
              kill(pid, SIGKILL);
              return 1; // what more can we do
            }

            // Ideally this triggers a dump, which may take a while,
            // so try and give the process extra time to clean up.
            nextStep = util::getWallTime() + std::max(15., MaxTime*.1);
          }
        }
      }

      return 0;
    }
  }

  sys::SetInterruptFunction(interrupt_handle);

  Module *mainModule = loadByteCode();
  mainModule = prepareModule(mainModule);

  // Get the desired main function.  klee_main initializes uClibc
  // locale and other data and then calls main.
  Function *mainFn = mainModule->getFunction("main");
  if (!mainFn) {
    std::cerr << "'main' function not found in module.\n";
    return 0;
  }

  // FIXME: Change me to std types.
  int pArgc;
  char **pArgv;
  char **pEnvp;
  readProgramArguments(pArgc, pArgv, pEnvp, envp);

  std::vector<bool> replayPath;

  llvm::sys::Path libraryPath(getKleeLibraryPath());

  klee::Interpreter::ModuleOptions mOpts(libraryPath.c_str(),
    /*Optimize=*/OptimizeModule,
    /*CheckDivZero=*/CheckDivZero);

  Interpreter::InterpreterOptions IOpts;
  IOpts.MakeConcreteSymbolic = MakeConcreteSymbolic;
  KleeHandler *handler = new KleeHandler(pArgc, pArgv);
  Interpreter *interpreter = 
    theInterpreter = Interpreter::create(IOpts, handler);
  handler->setInterpreter(interpreter);
  
  std::ostream &infoFile = handler->getInfoStream();
  for (int i=0; i<argc; i++) {
    infoFile << argv[i] << (i+1<argc ? " ":"\n");
  }
  infoFile << "PID: " << getpid() << "\n";

  const Module *finalModule = 
    interpreter->setModule(mainModule, mOpts);
  externalsAndGlobalsCheck(finalModule);

  char buf[256];
  time_t t[2];
  t[0] = time(NULL);
  strftime(buf, sizeof(buf), "Started: %Y-%m-%d %H:%M:%S\n", localtime(&t[0]));
  infoFile << buf;
  infoFile.flush();


  if (RunInDir != "") {
    int res = chdir(RunInDir.c_str());
    if (res < 0) {
      LOG(FATAL) << "Unable to change directory to: " << RunInDir.c_str();
    }
  }
  interpreter->runFunctionAsMain(mainFn, pArgc, pArgv, pEnvp);
      
  t[1] = time(NULL);
  strftime(buf, sizeof(buf), "Finished: %Y-%m-%d %H:%M:%S\n", localtime(&t[1]));
  infoFile << buf;

  strcpy(buf, "Elapsed: ");
  strcpy(format_tdiff(buf, t[1] - t[0]), "\n");
  infoFile << buf;

  delete interpreter;

  uint64_t queries = 
    *theStatisticManager->getStatisticByName("Queries");
  uint64_t queriesValid = 
    *theStatisticManager->getStatisticByName("QueriesValid");
  uint64_t queriesInvalid = 
    *theStatisticManager->getStatisticByName("QueriesInvalid");
  uint64_t queryCounterexamples = 
    *theStatisticManager->getStatisticByName("QueriesCEX");
  uint64_t queryConstructs = 
    *theStatisticManager->getStatisticByName("QueriesConstructs");
  uint64_t instructions = 
    *theStatisticManager->getStatisticByName("Instructions");
  uint64_t forks = 
    *theStatisticManager->getStatisticByName("Forks");

  handler->getInfoStream() 
    << "KLEE: done: explored paths = " << 1 + forks << "\n";

  // Write some extra information in the info file which users won't
  // necessarily care about or understand.
  if (queries)
    handler->getInfoStream() 
      << "KLEE: done: avg. constructs per query = " 
                             << queryConstructs / queries << "\n";  
  handler->getInfoStream() 
    << "KLEE: done: total queries = " << queries << "\n"
    << "KLEE: done: valid queries = " << queriesValid << "\n"
    << "KLEE: done: invalid queries = " << queriesInvalid << "\n"
    << "KLEE: done: query cex = " << queryCounterexamples << "\n";

  std::stringstream stats;
  stats << "\n";
  stats << "KLEE: done: total instructions = " 
        << instructions << "\n";
  stats << "KLEE: done: completed paths = " 
        << handler->getNumPathsExplored() << "\n";
  stats << "KLEE: done: generated tests = " 
        << handler->getNumTestCases() << "\n";
  std::cerr << stats.str();
  handler->getInfoStream() << stats.str();

  delete handler;

  return 0;
}
