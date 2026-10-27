# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# ysdbasic - YSD BASIC for OS-9 (written in C for CMOC): the BASIC of MK09 (mk09/apps/basic) ported to OS-9.
#
# The prebuilt modules (ysdbasic, ysdbasic-l2) are part of the distribution, so CMOC is only needed to change the program.
# CMOC (>= 0.1.100) needs lwtools on the PATH; tools/install_cmoc.sh builds both:
#     tools/install_cmoc.sh $HOME/cmoc
#     make -C os9/ysdbasic PATH=$HOME/cmoc/bin:$PATH
CMOC     ?= cmoc
CMOCOPTS  = --os9 --add-os9-stack-space=1500
# the version (BASIC_VERSION in ysdbasic.c, "1.0") as the edition of the module: 10 (ident shows it, tools/os9edition.py)
EDITION = $(shell sed -n 's/.*BASIC_VERSION *"\([0-9]*\)\.\([0-9]*\)".*/\1\2/p' ysdbasic.c | head -1)

all: ysdbasic ysdbasic-l2

# Level 1: what fits next to the module in 44 KB: a program of 3600 bytes (internal form), 80 number variables
ysdbasic: ysdbasic.c sfp.h sfpm.h types.h
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CMOCOPTS) -o ysdbasic ysdbasic.c"; \
	    $(CMOC) $(CMOCOPTS) -o ysdbasic ysdbasic.c || exit 1; \
	    python3 ../../tools/os9edition.py ysdbasic $(EDITION) || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt ysdbasic module"; test -f ysdbasic; \
	fi

# Level 2 has a 64K address space per process: a bigger program, more variables and array elements
ysdbasic-l2: ysdbasic.c sfp.h sfpm.h types.h
	@if command -v $(CMOC) >/dev/null 2>&1; then \
	    echo "$(CMOC) $(CMOCOPTS) -DPGM=12000 -DAPOOL=1000 -DNVARS=200 -o ysdbasic-l2 ysdbasic.c"; \
	    $(CMOC) $(CMOCOPTS) -DPGM=12000 -DAPOOL=1000 -DNVARS=200 -o ysdbasic-l2 ysdbasic.c || exit 1; \
	    python3 ../../tools/os9edition.py ysdbasic-l2 $(EDITION) || exit 1; \
	else \
	    echo "cmoc not found: keeping the prebuilt ysdbasic-l2 module"; test -f ysdbasic-l2; \
	fi

clean:
	rm -f *.o *.s *.lst *.map *.link
