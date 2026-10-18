# BASIC09 and RunB: the 6809 modules from the (unchanged) Microware sources in microware/.
# The Microware sources are not part of this repository: ./fetch.sh (git, network) puts them into microware/ (make does it
# when they are missing).  Needs lwtools (lwasm) and python3 on PATH; the prebuilt modules are part of the distribution.
# NITROS9DIR must have defs/os9.d (../nitros9-code does).
NITROS9DIR ?= $(abspath ../nitros9-code)
export NITROS9DIR

ifeq ($(shell command -v lwasm 2>/dev/null),)
# no lwasm: the prebuilt modules stay
all:
	@echo "lwasm is not installed: using the prebuilt basic09 and runb"
basic09 runb: ;
else
all: basic09 runb

microware/comand.asm:
	./fetch.sh

basic09: microware/comand.asm microware/compil.asm microware/binder.asm microware/stmts.asm microware/exprsn.asm microware/cnvio.asm microware/defs microware/b09defs microware/b09type microware/jmpdefs microware/linkage microware/editor
	python3 microware/build-modular-basic09.py basic09 $@ -DCOMMUNITY=1
runb: microware/comand.asm microware/compil.asm microware/binder.asm microware/stmts.asm microware/exprsn.asm microware/cnvio.asm microware/defs microware/b09defs microware/b09type microware/jmpdefs microware/linkage microware/editor
	python3 microware/build-modular-basic09.py runb $@ -DCOMMUNITY=1

endif

clean:
	rm -rf basic09 runb microware/.modular-build .modular-build
# (microware/ is kept: it is fetched, rm -rf microware ReadMe2 to fetch it again)
