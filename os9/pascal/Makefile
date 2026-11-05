# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# The Pascal compiler and the C compiler of MK09 on OS-9 Level 2:  pc  pas0  pas1  cg  as   cc  cpp  cc1   (the same sources as on MK09, mk09/apps, compiled by CMOC with --os9, plus os9shim.c)
# and the library of the programs that they make (lib/: CLIB.OBJ CLIB.IDX RT.OBJ RT.IDX START.S, put on the disk in /d0/LIB).
#
# The prebuilt modules and the library are part of the distribution, so CMOC is only needed to change a program (the library needs gcc and python3):
#     tools/install_cmoc.sh $HOME/cmoc
#     make -C os9/pascal PATH=$HOME/cmoc/bin:$PATH
CMOC   ?= cmoc
GCC    ?= gcc
PYTHON ?= python3
MK      = ../../mk09
APPS    = $(MK)/apps
INCS    = -Iinc -I$(MK)/include -I$(APPS)
CFLAGS  = --os9 -O2 -DOS9 $(INCS)
TOOLS   = pc pas0 pas1 cg as cc cpp cc1
LIBS    = lib/CLIB.OBJ lib/CLIB.IDX lib/RT.OBJ lib/RT.IDX lib/START.S
B       = build

all: $(TOOLS) $(LIBS)

# the stack of a tool (--add-os9-stack-space): pas1 recurses deeply;  cg and pas0 read with a bigger buffer
define tool
$(1): $(APPS)/$(1).c $(APPS)/ccio.h os9shim.c inc/mklib.h inc/mkstr.h $(APPS)/pastok.h $(wildcard $(APPS)/as_text.h)
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CFLAGS) $(2) -o $(1) $(APPS)/$(1).c os9shim.c"; \
	    $(CMOC) $(CFLAGS) $(2) -o $(1) $(APPS)/$(1).c os9shim.c || exit 1; \
	    $(PYTHON) ../../tools/os9edition.py $(1) $$$$(sed -n 's/.*TOOL_VERSION *"\([0-9]*\)\.\([0-9]*\)".*/\1\2/p' $(APPS)/$(1).c | head -1) || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt $(1)"; test -f $(1); \
	fi
endef
$(eval $(call tool,pc,--add-os9-stack-space=2000))
$(eval $(call tool,pas0,--add-os9-stack-space=2000 -DCC_BUF=512))
$(eval $(call tool,pas1,--add-os9-stack-space=3000))
$(eval $(call tool,cg,--add-os9-stack-space=2000 -DCC_BUF=512))
$(eval $(call tool,as,--add-os9-stack-space=2000))
$(eval $(call tool,cc,--add-os9-stack-space=2000))
$(eval $(call tool,cpp,--add-os9-stack-space=4000))
$(eval $(call tool,cc1,--add-os9-stack-space=8000))

# the library: made by the PC builds of cpp, cc1, cg and as (-DAS_OBJ), as for MK09 (mk09/Makefile), with -DOS9 (clib.c) and rt9.s (the system calls of OS-9)
$(B)/host_%: $(APPS)/%.c $(APPS)/ccio.h
	@mkdir -p $(B)
	$(GCC) -DCC_HOST -O1 -w -I$(APPS) -o $@ $<
$(B)/as_obj: $(APPS)/as.c $(APPS)/as_text.h
	@mkdir -p $(B)
	$(GCC) -DAS_HOST -DAS_OBJ -O1 -w -I$(APPS) -o $@ $<
$(B)/clib9.s: $(MK)/lib/cc/clib.c $(MK)/lib/cc/fp.c $(MK)/lib/cc/fpio.c $(B)/host_cpp $(B)/host_cc1 $(B)/host_cg
	cd $(B) && ./host_cpp -DOS9 -I../$(MK)/lib/cc ../$(MK)/lib/cc/clib.c -o clib9.i && ./host_cc1 clib9.i -o clib9.ir && ./host_cg -pl clib9.ir -o clib9.s
$(B)/rt9.s: $(MK)/lib/cc/rt.s $(MK)/lib/cc/rt9.s
	cat $(MK)/lib/cc/rt.s $(MK)/lib/cc/rt9.s > $@
lib/CLIB.OBJ lib/CLIB.IDX: $(B)/clib9.s $(B)/as_obj $(MK)/tools/mkobj.py
	@mkdir -p lib
	$(PYTHON) $(MK)/tools/mkobj.py $(B)/as_obj $(B)/clib9.s lib/CLIB.OBJ lib/CLIB.IDX
lib/RT.OBJ lib/RT.IDX: $(B)/rt9.s $(B)/as_obj $(MK)/tools/mkobj.py
	@mkdir -p lib
	$(PYTHON) $(MK)/tools/mkobj.py $(B)/as_obj $(B)/rt9.s lib/RT.OBJ lib/RT.IDX
lib/START.S: $(MK)/lib/cc/start.s
	@mkdir -p lib
	cp $< $@

clean:
	rm -rf $(B)

.PHONY: all clean
