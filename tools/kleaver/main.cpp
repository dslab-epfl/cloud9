#include <iostream>

#include "expr/Lexer.h"
#include "expr/Parser.h"

#include "klee/Constraints.h"
#include "klee/Expr.h"
#include "klee/ExprBuilder.h"
#include "klee/Solver.h"
#include "klee/SolverImpl.h"
#include "klee/Statistics.h"
#include "klee/util/ExprPPrinter.h"
#include "klee/util/ExprVisitor.h"
#include "klee/util/Assignment.h"
#include "klee/Internal/System/Time.h"

// FIXME: Ugh, this is gross. But otherwise our config.h conflicts with LLVMs.
#undef PACKAGE_BUGREPORT
#undef PACKAGE_NAME
#undef PACKAGE_STRING
#undef PACKAGE_TARNAME
#undef PACKAGE_VERSION

#include "llvm/ADT/OwningPtr.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ManagedStatic.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/system_error.h"

#include <fstream>
#include <iostream>

using namespace llvm;
using namespace klee;
using namespace klee::expr;

namespace {
  llvm::cl::opt<std::string>
  InputFile(llvm::cl::desc("<input query log>"), llvm::cl::Positional,
            llvm::cl::init("-"));

  enum ToolActions {
    PrintTokens,
    PrintAST,
    Evaluate
  };

  static llvm::cl::opt<ToolActions> 
  ToolAction(llvm::cl::desc("Tool actions:"),
             llvm::cl::init(Evaluate),
             llvm::cl::values(
             clEnumValN(PrintTokens, "print-tokens",
                        "Print tokens from the input file."),
             clEnumValN(PrintAST, "print-ast",
                        "Print parsed AST nodes from the input file."),
             clEnumValN(Evaluate, "evaluate",
                        "Print parsed AST nodes from the input file."),
             clEnumValEnd));

  enum BuilderKinds {
    DefaultBuilder,
    ConstantFoldingBuilder,
    SimplifyingBuilder
  };

  static llvm::cl::opt<BuilderKinds> 
  BuilderKind("builder",
              llvm::cl::desc("Expression builder:"),
              llvm::cl::init(DefaultBuilder),
              llvm::cl::values(
              clEnumValN(DefaultBuilder, "default",
                         "Default expression construction."),
              clEnumValN(ConstantFoldingBuilder, "constant-folding",
                         "Fold constant expressions."),
              clEnumValN(SimplifyingBuilder, "simplify",
                         "Fold constants and simplify expressions."),
              clEnumValEnd));

  cl::opt<bool>
  UseDummySolver("use-dummy-solver",
       cl::init(false));

  cl::opt<bool>
  UseFastCexSolver("use-fast-cex-solver",
       cl::init(false));

  cl::opt<std::string>
  LogQueryPC("log-query-pc");

  cl::opt<std::string>
  LogOutput("log-output");

  cl::opt<bool>
  ValidateAssignments("validate-assignments", cl::init(false));
}

static std::string escapedString(const char *start, unsigned length) {
  std::string Str;
  llvm::raw_string_ostream s(Str);
  for (unsigned i=0; i<length; ++i) {
    char c = start[i];
    if (isprint(c)) {
      s << c;
    } else if (c == '\n') {
      s << "\\n";
    } else {
      s << "\\x" 
        << hexdigit(((unsigned char) c >> 4) & 0xF) 
        << hexdigit((unsigned char) c & 0xF);
    }
  }
  return s.str();
}

static void PrintInputTokens(const MemoryBuffer *MB) {
  Lexer L(MB);
  Token T;
  do {
    L.Lex(T);
    std::cout << "(Token \"" << T.getKindName() << "\" "
               << "\"" << escapedString(T.start, T.length) << "\" "
               << T.length << " "
               << T.line << " " << T.column << ")\n";
  } while (T.kind != Token::EndOfFile);
}

static bool PrintInputAST(const char *Filename,
                          const MemoryBuffer *MB,
                          ExprBuilder *Builder) {
  std::vector<Decl*> Decls;
  Parser *P = Parser::Create(Filename, MB, Builder, false);
  P->SetMaxErrors(20);

  unsigned NumQueries = 0;
  while (Decl *D = P->ParseTopLevelDecl()) {
    if (!P->GetNumErrors()) {
      if (isa<QueryCommand>(D))
        std::cout << "# Query " << ++NumQueries << "\n";

      D->dump();
    }
    Decls.push_back(D);
  }

  bool success = true;
  if (unsigned N = P->GetNumErrors()) {
    std::cerr << Filename << ": parse failure: "
               << N << " errors.\n";
    success = false;
  }

  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it)
    delete *it;

  delete P;

  return success;
}

// Logging solver producing JSON output
class JSONLoggingSolver : public SolverImpl {
  Solver *solver;
  double startTime;
  std::ofstream os;
  int queryCount;

public:
  JSONLoggingSolver(Solver *_solver, std::string path) : 
      solver(_solver),
      os(path.c_str(), std::ios::trunc),
      queryCount(0) {
    os << "[\n";
  }

  ~JSONLoggingSolver() {
    delete solver;
  }


  void startQuery(const Query& query,
                  const char *typeName) {

    os << "\t[" << queryCount++ << ",\"" << typeName << "\",";
    os.flush();
    startTime = klee::util::getWallTime();
  }

  void finishQuery(const Query& query, bool success) {
    double delta = klee::util::getWallTime() - startTime;
    os << "\"" << (success ? "OK" : "FAIL") << "\"," << delta << ",";
		os << "[]";
    os << "],\n";
    os.flush();
  }

  bool computeTruth(const Query& query, bool &isValid) {
    startQuery(query, "Truth");
    bool success = solver->impl->computeTruth(query, isValid);
    if (success)
      os << "\"" << (isValid ? "true" : "false") << "\",";
    else
      os << "None,";
    finishQuery(query, success);
    return success;
  }

  bool computeValidity(const Query& query, Solver::Validity &result) {
    startQuery(query, "Validity");
    bool success = solver->impl->computeValidity(query, result);
    if (success)
      os << result << ",";
    else
      os << "None,";
    finishQuery(query, success);
    return success;
  }

  bool computeValue(const Query& query, ref<Expr> &result) {
    startQuery(query.withFalse(), "Value");
    bool success = solver->impl->computeValue(query, result);
    if (success)
      os << result << ",";
    else
      os << "None,";
    finishQuery(query, success);
    return success;
  }

  bool computeInitialValues(const Query& query,
                            const std::vector<const Array*> &objects,
                            std::vector< std::vector<unsigned char> > &values,
                            bool &hasSolution) {
    startQuery(query, "InitialValues");
    bool success = solver->impl->computeInitialValues(query, objects,
                                                      values, hasSolution);
    if (success) {
      if (hasSolution) {
        std::vector< std::vector<unsigned char> >::iterator
          values_it = values.begin();
        os << "{";
        for (std::vector<const Array*>::const_iterator i = objects.begin(),
               e = objects.end(); i != e; ++i, ++values_it) {
          const Array *array = *i;
          std::vector<unsigned char> &data = *values_it;
          os << "\"" << array->name << "\": [";
          for (unsigned j = 0; j < array->size; j++) {
            os << (int) data[j];
            if (j+1 < array->size)
              os << ",";
          }
          os << "],";
        }
        os << "},";
      } else {
        os << "{},";
      }
    } else {
      os << "None,";
    }
    finishQuery(query, success);
    return success;
  }
};

static bool EvaluateInputAST(const char *Filename,
                             const MemoryBuffer *MB,
                             ExprBuilder *Builder) {
  std::vector<Decl*> Decls;
  Parser *P = Parser::Create(Filename, MB, Builder, false);
  P->SetMaxErrors(20);
  while (Decl *D = P->ParseTopLevelDecl()) {
    Decls.push_back(D);
  }

  bool success = true;
  if (unsigned N = P->GetNumErrors()) {
    std::cerr << Filename << ": parse failure: "
               << N << " errors.\n";
    success = false;
  }  

  if (!success)
    return false;

#if 0
  // FIXME: Support choice of solver.
  Solver *S, *STP = S = 
    UseDummySolver ? createDummySolver() : new STPSolver(false);
#endif
  
  Solver *S;
  Solver *O = NULL;
  if (UseDummySolver) {
    S = createDummySolver();
  } else {
    S = new STPSolver(false);
  }

  if (LogQueryPC != "" && !UseDummySolver) {
    S = createPCLoggingSolver(S, LogQueryPC);
  }

  if (LogOutput != "" && !UseDummySolver) {
    S = new Solver(new JSONLoggingSolver(S, LogOutput));
  }

#if 1
  if (UseFastCexSolver)
    S = createFastCexSolver(S);
  S = createCexCachingSolver(S);
  S = createCachingSolver(S);
  S = createIndependentSolver(S);
#endif

  unsigned Index = 0;
  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it) {
    Decl *D = *it;
    if (QueryCommand *QC = dyn_cast<QueryCommand>(D)) {
      std::cout << "Query " << Index << ":\t";

      assert("FIXME: Support counterexample query commands!");
      if (QC->Values.empty() && QC->Objects.empty()) {
        bool result;
        if (S->mustBeTrue(Query(ConstraintManager(QC->Constraints), 
                                QC->Query),
                          result)) {
          std::cout << (result ? "VALID" : "INVALID");
        } else {
          std::cout << "FAIL";
        }
      } else if (!QC->Values.empty()) {
        assert(QC->Objects.empty() && 
               "FIXME: Support counterexamples for values and objects!");
        assert(QC->Values.size() == 1 &&
               "FIXME: Support counterexamples for multiple values!");
        assert(QC->Query->isFalse() &&
               "FIXME: Support counterexamples with non-trivial query!");
        ref<ConstantExpr> result;
        if (S->getValue(Query(ConstraintManager(QC->Constraints), 
                              QC->Values[0]),
                        result)) {
          std::cout << "INVALID\n";
          std::cout << "\tExpr 0:\t" << result;
        } else {
          std::cout << "FAIL";
        }
      } else {
        std::vector< std::vector<unsigned char> > result;
        
        ConstraintManager originalCM = ConstraintManager(QC->Constraints);
        Query origQuery = Query(originalCM,
                                QC->Query);

        if (S->getInitialValues(origQuery,
                                QC->Objects, result)) {
          std::cout << "INVALID\n";

          for (unsigned i = 0, e = result.size(); i != e; ++i) {
            std::cout << "\tArray " << i << ":\t"
                       << QC->Objects[i]->name
                       << "[";
            for (unsigned j = 0; j != QC->Objects[i]->size; ++j) {
              std::cout << (unsigned) result[i][j];
              if (j + 1 != QC->Objects[i]->size)
                std::cout << ", ";
            }
            std::cout << "]";
            if (i + 1 != e)
              std::cout << "\n";
          }

          if (ValidateAssignments) {
            Assignment assign((std::vector<const Array*> &) QC->Objects, result);
            bool sat1 = assign.satisfies(QC->Constraints.begin(), 
                                         QC->Constraints.end());
            bool sat2 = assign.evaluate(NotExpr::create(QC->Query))->isTrue();
            assert(sat1 && "Assignment does not satisfy constraints");
            assert(sat2 && "Query is not true");
          }
        } else {
          std::cout << "FAIL";
        }
      }

      std::cout << "\n";
      ++Index;
    }
  }

  for (std::vector<Decl*>::iterator it = Decls.begin(),
         ie = Decls.end(); it != ie; ++it)
    delete *it;
  delete P;

  delete S;
  if (O) {
    delete O;
  }

  if (uint64_t queries = *theStatisticManager->getStatisticByName("Queries")) {
    std::cout 
      << "--\n"
      << "total queries = " << queries << "\n"
      << "total queries constructs = " 
      << *theStatisticManager->getStatisticByName("QueriesConstructs") << "\n"
      << "valid queries = " 
      << *theStatisticManager->getStatisticByName("QueriesValid") << "\n"
      << "invalid queries = " 
      << *theStatisticManager->getStatisticByName("QueriesInvalid") << "\n"
      << "query cex = " 
      << *theStatisticManager->getStatisticByName("QueriesCEX") << "\n";
  }

  return success;
}

int main(int argc, char **argv) {
  bool success = true;

  llvm::sys::PrintStackTraceOnErrorSignal();
  llvm::cl::ParseCommandLineOptions(argc, argv);

  std::string ErrorStr;
  OwningPtr<MemoryBuffer> MB;
  error_code ec=MemoryBuffer::getFileOrSTDIN(InputFile.c_str(), MB);
  if (ec) {
    std::cerr << argv[0] << ": error: " << ec.message() << "\n";
    return 1;
  }

  ExprBuilder *Builder = 0;
  switch (BuilderKind) {
  case DefaultBuilder:
    Builder = createDefaultExprBuilder();
    break;
  case ConstantFoldingBuilder:
    Builder = createDefaultExprBuilder();
    Builder = createConstantFoldingExprBuilder(Builder);
    break;
  case SimplifyingBuilder:
    Builder = createDefaultExprBuilder();
    Builder = createConstantFoldingExprBuilder(Builder);
    Builder = createSimplifyingExprBuilder(Builder);
    break;
  }

  switch (ToolAction) {
  case PrintTokens:
    PrintInputTokens(MB.get());
    break;
  case PrintAST:
    success = PrintInputAST(InputFile=="-" ? "<stdin>" : InputFile.c_str(), MB.get(),
                            Builder);
    break;
  case Evaluate:
    success = EvaluateInputAST(InputFile=="-" ? "<stdin>" : InputFile.c_str(),
                               MB.get(), Builder);
    break;
  default:
    std::cerr << argv[0] << ": error: Unknown program action!\n";
  }

  delete Builder;

  llvm::llvm_shutdown();
  return success ? 0 : 1;
}
