##===- unittests/Expr/Makefile -----------------------------*- Makefile -*-===##

LEVEL=../..
TESTNAME = Expr

include $(LEVEL)/Makefile.config

STP_LIBS := stp_c_interface.a stp_AST.a stp_bitvec.a \
            stp_constantbv.a stp_sat.a stp_simplifier.a


USEDLIBS := kleaverExpr.a kleeBasic.a
ifeq ($(ENABLE_EXT_STP),0)
  USEDLIBS += $(STP_LIBS)
endif
USEDLIBS += cloud9common.a

LINK_COMPONENTS = support

include $(LLVM_SRC_ROOT)/unittests/Makefile.unittest

LIBS += -lboost_thread-mt -lboost_system-mt

ifeq ($(ENABLE_EXT_STP),1)
  LIBS += -lstp
endif
