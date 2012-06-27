LEVEL=../..
TOOLNAME = kexpr-tool

include $(LEVEL)/Makefile.config



USEDLIBS = kleeData.a kleeCore.a kleaverExpr.a

LINK_COMPONENTS = ipo

include $(LEVEL)/Makefile.common

LIBS += -lglog -lprotobuf

# This dependency is ridiculous. We must refactor the Expr library.
ifeq ($(ENABLE_EXT_STP),1)
  LIBS += -lstp
endif
