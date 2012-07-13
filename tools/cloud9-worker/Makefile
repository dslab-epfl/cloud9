LEVEL = ../..
TOOLNAME = c9-worker

include $(LEVEL)/Makefile.config

STP_LIBS := stp_c_interface.a stp_AST.a stp_bitvec.a stp_constantbv.a stp_sat.a \
        stp_simplifier.a


USEDLIBS = cloud9worker.a cloud9common.a \
					 kleeCore.a kleeData.a kleeModule.a kleaverSolver.a kleaverExpr.a \
					 kleeSupport.a kleeBasic.a 

ifeq ($(ENABLE_EXT_STP),0)
	USEDLIBS += $(STP_LIBS)
endif

LINK_COMPONENTS = jit engine bitreader bitwriter linker ipo

include $(LEVEL)/Makefile.common

LIBS += -lboost_system-mt -lboost_thread-mt -lprotobuf -lglog -lrt -lcrypto++ -lboolector # -lprofiler
ifeq ($(ENABLE_EXT_STP),1)
  LIBS += -lstp
endif
