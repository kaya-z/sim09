# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# MK09 - a microkernel for the MC6809 (sim09 / Tang Nano 9K).  Build:  make   (needs CMOC and lwtools:
# tools/install_cmoc.sh PREFIX in the parent directory installs both;  make CMOC_BIN=PREFIX/bin)
#
#   make            kernel, servers, programs, boot ROM, disk image (build/)
#   make test       run the tests (needs ../sim09)
#   make run        boot the disk image in the simulator

CMOC_BIN ?=
CMOC   := $(if $(CMOC_BIN),$(CMOC_BIN)/,)cmoc
LWASM  := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwasm
LWLINK := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwlink
LWAR   := $(if $(CMOC_BIN),$(CMOC_BIN)/,)lwar
PYTHON ?= python3 -X utf8
# BOARD=sbc09 (default: sim09 / Tang Nano 9K) or BOARD=fm11 (FUJITSU FM-11, see fm11/README.md); the FM-11 is built in build-fm11
BOARD  ?= sbc09
# BOOTDEV=hd (default, the full system on the hard disk, 16MB) or 2hd (BOARD=fm11 only: a reduced system on the 2HD floppy disk, 1MB)
BOOTDEV ?= hd
B      ?= build$(if $(filter fm11,$(BOARD)),-fm11$(if $(filter 2hd,$(BOOTDEV)),-2hd))
BSPDIR := bsp/$(BOARD)
MEMMAP := $(if $(filter fm11,$(BOARD)),memmap_fm11.mk,memmap.mk)
SIM    ?= ../sim09

CMOCLIB := $(shell dirname "$$(command -v $(CMOC) 2>/dev/null)" 2>/dev/null)/../share/cmoc/lib
INCS    := -Itests -Iinclude -Iarch/m6809 -I$(BSPDIR) -Iapps/basic -I$(B)
CFLAGS  := --lwasm=$(LWASM) --void-target --no-relocate -O2 -fomit-frame-pointer -Dmain=mk_main $(INCS)
ASFLAGS := -fobj -I$(B) -Iinclude
ifeq ($(BOARD),fm11)
CFLAGS  += -DBOARD_FM11=1
ASFLAGS += -DFM11=1
ifeq ($(BOOTDEV),2hd)
CFLAGS  += -DFM11_2HD=1
endif
endif

vpath %.c kernel arch/m6809 $(BSPDIR) servers lib apps apps/jwp apps/basic
vpath %.s arch/m6809 lib apps/basic servers

# the addresses of the memory map (computed from the slot sizes in memmap.mk)
-include $(B)/memmap.gen.mk
HDRS := $(wildcard include/*.h arch/m6809/*.h $(BSPDIR)/*.h) $(B)/memmap.h $(B)/memmap.inc $(B)/sysnum.inc $(B)/version.inc

ifeq ($(BOARD),fm11)
ifeq ($(BOOTDEV),2hd)
all: $(B)/boot.img $(B)/ipl_2hd.bin $(B)/mk09_2hd.img
else
all: $(B)/boot.img $(B)/ipl_hdd.bin $(B)/mk09.img
endif
else
all: $(B)/boot.img $(B)/bootrom.bin $(B)/mk09.img
endif

$(B):
	mkdir -p $(B)

$(B)/memmap.h $(B)/memmap.inc $(B)/memmap.gen.mk: $(MEMMAP) tools/genmap.py | $(B)
	$(PYTHON) tools/genmap.py $(MEMMAP) $(B) $(MAPOVER)

$(B)/version.inc: include/version.h | $(B)
	sed -n 's/^\#define \(MX_VERSION\)[ \t]*\([0-9]*\).*/\1\tequ\t\2/p' $< > $@

$(B)/sysnum.inc: include/sysnum.h | $(B)
	sed -n 's/^\#define \(SYS_[A-Z]*\)[ \t]*\(.*\)/\1\tequ\t\2/p' $< > $@

$(B)/%.o: %.c $(HDRS) $(wildcard apps/basic/*.h)
	$(CMOC) -c $(CFLAGS) -o $@ $<

# the FM-11 has 38KB for the programs (the I/O area $FC00-$FFFF is in the way): BASIC does not fit with its math functions, so it is built without them
ifeq ($(BOARD),fm11)
$(B)/basic.o: CFLAGS += -DNOMATH
endif

# BASIC without SIN COS TAN ATN EXP LOG SQR and non-integer powers (about 3.5 KB of code): room for a bigger program and bigger arrays
#   make BASIC_NOMATH=1
ifdef BASIC_NOMATH
$(B)/basic.o: CFLAGS += -DNOMATH -DPGM=6000 -DAPOOL=700
endif

$(B)/%.o: %.s $(HDRS)
	$(LWASM) $(ASFLAGS) -o $@ $<

$(B)/bootrom.bin: boot/bootrom.s | $(B)
	$(LWASM) --format=raw -o $@ $<

# FM-11: the boot ROM of the machine loads 512 bytes from the hard disk (cylinder 0, head 0, sectors 0 and 1) to $0400 and jumps there
$(B)/ipl_hdd.bin: boot/ipl_fm11_hdd.s $(B)/memmap.inc | $(B)
	$(LWASM) --format=raw -I$(B) -o $@ $<

# FM-11 2HD: the boot ROM reads the sectors 1-4 of track 0 side 0 (128 bytes each) to $0400 and jumps there
$(B)/ipl_2hd.bin: boot/ipl_fm11_2hd.s | $(B)
	$(LWASM) --format=raw -I$(B) -o $@ $<

# ---- the library for the servers and programs ----
SYSSTUBS := mk_send mk_recv mk_call mk_reply mk_notify mk_tinfo mk_tkill mk_callg mk_copyfrom mk_copyto mk_tload \
	mk_sleep mk_ticks mk_yield mk_exit mk_self mk_hook mk_rxreg mk_tstart mk_tcreate mk_treap mk_power
MEMFUNCS := memcpy memmove memset
LIBMK_OBJS := $(patsubst %,$(B)/sys_%.o,$(SYSSTUBS)) $(patsubst %,$(B)/mem_%.o,$(MEMFUNCS)) \
	$(patsubst lib/%.c,$(B)/%.o,$(wildcard lib/mk_*.c))

$(B)/sys_%.o: lib/mksys.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DSTUB_$*=1 -o $@ $<
$(B)/mem_%.o: lib/mem.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DFUNC_$*=1 -o $@ $<
$(B)/libmk.a: $(LIBMK_OBJS)
	rm -f $@; $(LWAR) -c $@ $^

# ---- system modules: name, objects, code slot, data slot ----
# KERNEL=asm (default): the kernel in 6809 assembler; KERNEL=c: the portable C kernel
KERNEL ?= asm
ifneq ($(KERNEL),asm)
MAPOVER := K_CSZ=1060 K_DSZ=02F0 HIGH=PM
endif
ifeq ($(KERNEL),asm)
KERNEL_OBJS := $(B)/kernel_asm.o
$(B)/kernel_asm.o: arch/m6809/kernel.s $(HDRS)
	$(LWASM) $(ASFLAGS) -o $@ $<
else
KERNEL_OBJS := $(B)/entry.o $(B)/arch.o $(B)/kernel.o $(B)/bsp.o
endif
TTY_OBJS    := $(B)/crt0.o $(B)/tty.o
DISK_OBJS   := $(B)/crt0.o $(B)/disk.o
ifeq ($(BOARD),fm11)
TTY_OBJS    += $(B)/console.o
DISK_OBJS   += $(B)/bsp_disk.o $(B)/fdc.o
ifneq ($(BOOTDEV),2hd)
DISK_OBJS   += $(B)/mdc.o
endif
endif
FS_OBJS     := $(B)/crt0.o $(B)/fs.o $(B)/fs_core.o $(B)/rtc.o
PM_OBJS     := $(B)/crt0.o $(B)/pm.o
MM_OBJS     := $(B)/mm.o

LINK = $(LWLINK) --format=srec --script=$(B)/$(1).link --map=$(B)/$(1).map -o $$@ $$($(2)_OBJS) -L$(B) -lmk -L$(CMOCLIB) -lcmoc-crt-void -lcmoc-std-void

define SYSMOD
$(B)/$(1).link: $(MEMMAP) tools/mklink.py | $(B)
	$(PYTHON) tools/mklink.py $$($(2)_CODE) $$($(2)_DATA) $$($(2)_START) > $$@
$(B)/$(1).s19: $$($(2)_OBJS) $(B)/libmk.a $(B)/$(1).link tools/chkmap.py
	$(call LINK,$(1),$(2))
	$(PYTHON) tools/chkmap.py $(1) $(B)/$(1).map $$($(2)_CODE) $$($(2)_CSZ) $$($(2)_DATA) $$($(2)_DSZ) $(3)
endef

# kernel code slot symbol names: K_ -> KERNEL
KERNEL_CODE := $(K_CODE)
KERNEL_CSZ  := $(K_CSZ)
KERNEL_DATA := $(K_DATA)
KERNEL_DSZ  := $(K_DSZ)
# the kernel in assembler has the code that runs once at the boot (the table of vectors, the first tasks) in the section 'start': it is loaded into the
# transient area (behind the system code), where the first program overwrites it: it needs no room in the slot of the kernel
ifeq ($(KERNEL),asm)
KERNEL_START := $(TPA_BASE)
endif
$(eval $(call SYSMOD,kernel,KERNEL,0))
$(eval $(call SYSMOD,tty,TTY,B4))
$(eval $(call SYSMOD,disk,DISK,82))
$(eval $(call SYSMOD,fs,FS,140))
$(eval $(call SYSMOD,pm,PM,C8))
$(eval $(call SYSMOD,mm,MM,30))

SYSMODS := kernel tty disk fs pm mm

# ---- programs (MX executables): apps/NAME.c -> build/NAME.mx ----
APPS      := sh as cpp cc1 cg cc pas0 pas1 pmsg pc ls cat echo stty cp mv rm mkdir date df ps hd wc spin stk fstest faults ipcbench dhry more grep life basic kill mem reboot sfptest jwp
# the 2HD floppy disk of the FM-11 (1MB): the programs that fit, without jwp (and its dictionary) and the test programs
ifeq ($(BOARD),fm11)
APPS      += format mount umount
endif
ifeq ($(BOOTDEV),2hd)
APPS      := sh as cpp cc1 cg cc pas0 pas1 pmsg pc ls cat echo stty cp mv rm mkdir date df ps hd wc more grep kill mem reboot basic format mount umount
SKKJISYO  :=
endif
STACK_sh  := 640
STACK_jwp := 1200
STACK_basic := 1500
STACK_as := 700
STACK_cpp := 4000
STACK_cc1 := 2100
STACK_pas1 := 750
STACK_pas0 := 700
STACK_pmsg := 500
STACK_cg := 2200
$(B)/as.o: apps/as_text.h
$(B)/cg.o $(B)/pas0.o: CFLAGS += -DCC_BUF=512
OBJS_basic := $(B)/sfpa.o
OBJS_sfptest := $(B)/sfpa.o

# jwp: the character set of the console and of the dictionary (utf8, euc or sjis) and the size
# of the text buffer
CHARSET   ?= $(if $(filter fm11,$(BOARD)),sjis,utf8)
# (the FM-11 has 156 pages for programs, 8 fewer than the sbc09: a smaller text buffer)
JWP_BUF   ?= $(if $(filter fm11,$(BOARD)),7500,11500)
JWP_OPT   ?= -fomit-frame-pointer
APPBASE    = $(or $(BASE_$(1)),$(TPA_BASE))
APPSTACK   = $(or $(STACK_$(1)),512)
APPLIBS   := -L$(B) -lmk -L$(CMOCLIB) -lcmoc-crt-void -lcmoc-std-void

define APP
$(B)/crt0app_$(1).o: crt0app.s $(HDRS)
	$(LWASM) $(ASFLAGS) -DSTACKSZ=$(call APPSTACK,$(1)) -DMXFLAGS=$(or $(FLAGS_$(1)),0) -o $$@ $$<
$(B)/$(1).link: $(MEMMAP) tools/mklink.py | $(B)
	$(PYTHON) tools/mklink.py $(TPA_BASE) > $$@
$(B)/$(1).link2: $(MEMMAP) tools/mklink.py | $(B)
	$(PYTHON) tools/mklink.py $$$$(printf '%X' $$$$((0x$(TPA_BASE) + 256))) > $$@
# the program is linked twice, 256 bytes apart; tools/mkmx.py makes the relocation table from the difference
$(B)/$(1).mx: $(B)/crt0app_$(1).o $(B)/$(1).o $(OBJS_$(1)) $(B)/libmk.a $(B)/$(1).link $(B)/$(1).link2 tools/mkmx.py
	$(LWLINK) --format=raw --script=$(B)/$(1).link --map=$(B)/$(1).map -o $(B)/$(1).raw1 $(B)/crt0app_$(1).o $(B)/$(1).o $(OBJS_$(1)) $(APPLIBS)
	$(LWLINK) --format=raw --script=$(B)/$(1).link2 -o $(B)/$(1).raw2 $(B)/crt0app_$(1).o $(B)/$(1).o $(OBJS_$(1)) $(APPLIBS)
	$(PYTHON) tools/mkmx.py $(B)/$(1).raw1 $(B)/$(1).raw2 -o $$@ > /dev/null
endef
$(foreach a,$(APPS),$(eval $(call APP,$(a))))

$(B)/msgs_$(CHARSET).h: apps/jwp/genmsgs.py | $(B)
	$(PYTHON) apps/jwp/genmsgs.py $(CHARSET) > $@
$(B)/msgs.h: $(B)/msgs_$(CHARSET).h
	cp $< $@
$(B)/jwp.o: jwp.c skk.c $(B)/msgs.h $(HDRS)
	$(CMOC) -c --lwasm=$(LWASM) --void-target -O2 $(JWP_OPT) -DBUFSZ=$(JWP_BUF) -Dmain=mk_main $(INCS) -Iapps/jwp -o $@ $<

# ---- the file tree of the disk ----
# SKK dictionary for jwp (tools/mkskkdic.py, from the Debian package skkdic: SKK-JISYO.L); SKK_KB
# limits its size (0 = everything, about 4.5MB)
ICONV_utf8 = UTF-8
ICONV_euc = EUC-JP
ICONV_sjis = CP932
SKKJISYO ?= /usr/share/skk/SKK-JISYO.L
SKK_KB   ?= 0
HAVE_SKK := $(wildcard $(SKKJISYO))
$(B)/skk_$(CHARSET).dic: ../tools/mkskkdic.py $(SKKJISYO)
	$(PYTHON) ../tools/mkskkdic.py -e $(CHARSET) -b $(SKK_KB) -o $@ $(SKKJISYO)

# the library of the C compiler: assembler text made by the PC builds of cpp, cc1 and cg (-DCC_HOST), put on the disk in /LIB
# cc1 does not fit in the transient area with its error messages: they are taken out (tools/msgsplit.py) into /LIB/CC1.MSG
$(B)/cc1_t.c $(B)/cc1.msg: apps/cc1.c tools/msgsplit.py | $(B)
	$(PYTHON) tools/msgsplit.py apps/cc1.c $(B)/cc1_t.c $(B)/cc1.msg
$(B)/cc1.o: $(B)/cc1_t.c apps/ccio.h $(HDRS)
	$(CMOC) -c $(CFLAGS) -DMSGS -Iapps -o $@ $<

$(B)/pas1_t.c $(B)/pas1.msg: apps/pas1.c tools/msgsplit.py | $(B)
	$(PYTHON) tools/msgsplit.py apps/pas1.c $(B)/pas1_t.c $(B)/pas1.msg
$(B)/pas1.o: $(B)/pas1_t.c apps/ccio.h $(HDRS)
	$(CMOC) -c $(CFLAGS) -DMSGS -Iapps -o $@ $<

# the library as blocks that are assembled once, by the PC build of as with -DAS_OBJ (tools/mkobj.py): OBJ and an index for cg to link
$(B)/as_obj: apps/as.c apps/as_text.h | $(B)
	gcc -DAS_HOST -DAS_OBJ -O1 -w -Iapps -o $@ $<
$(B)/clib.obj $(B)/clib.idx: $(B)/clib.s $(B)/as_obj tools/mkobj.py
	$(PYTHON) tools/mkobj.py $(B)/as_obj $(B)/clib.s $(B)/clib.obj $(B)/clib.idx
$(B)/rt.obj $(B)/rt.idx: lib/cc/rt.s $(B)/as_obj tools/mkobj.py
	$(PYTHON) tools/mkobj.py $(B)/as_obj lib/cc/rt.s $(B)/rt.obj $(B)/rt.idx

$(B)/host_%: apps/%.c apps/ccio.h | $(B)
	gcc -DCC_HOST -O1 -w -Iapps -o $@ $<
$(B)/clib.s: lib/cc/clib.c $(B)/host_cpp $(B)/host_cc1 $(B)/host_cg
	cd $(B) && ./host_cpp ../lib/cc/clib.c -o clib.i && ./host_cc1 clib.i -o clib.ir && ./host_cg -pl clib.ir -o clib.s

TREE := $(B)/tree
$(B)/tree.stamp: $(foreach a,$(APPS),$(B)/$(a).mx) $(wildcard disk/etc/* disk/doc/* disk/bas/* disk/asm/* disk/inc/* disk/pas/*) $(if $(HAVE_SKK),$(B)/skk_$(CHARSET).dic) $(B)/clib.obj $(B)/rt.obj $(B)/cc1.msg $(B)/pas1.msg lib/cc/start.s Makefile
	rm -rf $(TREE); mkdir -p $(TREE)/bin $(TREE)/etc $(TREE)/doc $(TREE)/bas $(TREE)/asm $(TREE)/inc $(TREE)/pas $(TREE)/lib $(TREE)/skk
	cp lib/cc/start.s $(TREE)/lib/start.s; cp $(B)/cc1.msg $(TREE)/lib/cc1.msg; cp $(B)/pas1.msg $(TREE)/lib/pas1.msg; cp $(B)/clib.obj $(B)/clib.idx $(B)/rt.obj $(B)/rt.idx $(TREE)/lib/
	for a in $(APPS); do cp $(B)/$$a.mx $(TREE)/bin/$$a.mx; done
	for d in etc doc bas asm inc pas; do if [ -d disk/$$d ] && [ -n "$$(ls disk/$$d)" ]; then cp disk/$$d/* $(TREE)/$$d/; fi; done
	printf '# /ETC/RC.SH - run by the shell when the system starts\nstty $(CHARSET)\n' > $(TREE)/etc/rc.sh
	if [ -n "$(HAVE_SKK)" ]; then cp $(B)/skk_$(CHARSET).dic $(TREE)/skk/skk.dic; fi
	iconv -f UTF-8 -t $(ICONV_$(CHARSET)) disk/doc/guide.txt > $(TREE)/doc/guide.txt
	touch $@

$(B)/mk09.img: $(B)/boot.img $(B)/tree.stamp tools/mkdisk.py $(if $(filter fm11,$(BOARD)),$(B)/ipl_hdd.bin)
	$(PYTHON) tools/mkdisk.py -o $@ --boot $(B)/boot.img $(if $(filter fm11,$(BOARD)),--ipl $(B)/ipl_hdd.bin) --size-mb $(DISK_MB) $(TREE)

# the 2HD floppy disk image of the FM-11 (1021696 bytes, FDD_2HD_256 of the simulator): track 0 side 0 (26 sectors of 128 bytes) holds the
# IPL, the 3978 sectors of 256 bytes behind it are the MK09 disk: MBR, boot image, a small FAT16 volume
$(B)/mk09_disk2hd.img: $(B)/boot.img $(B)/tree.stamp tools/mkdisk.py
	$(PYTHON) tools/mkdisk.py -o $@ --boot $(B)/boot.img --part-start 48 --total-blocks 1989 --spc 4 --small $(TREE)
$(B)/mk09_2hd.img: $(B)/mk09_disk2hd.img $(B)/ipl_2hd.bin tools/mk2hd.py
	$(PYTHON) tools/mk2hd.py -o $@ --ipl $(B)/ipl_2hd.bin $(B)/mk09_disk2hd.img

DISK_MB ?= 16

$(B)/boot.img: $(foreach m,$(SYSMODS),$(B)/$(m).s19) tools/mkimage.py
	$(PYTHON) tools/mkimage.py -o $@ -e 0x$(if $(KERNEL_START),$(KERNEL_START),$(K_CODE)) $(foreach m,$(SYSMODS),$(B)/$(m).s19)

run: all
	$(SIM) -rom $(B)/bootrom.bin -0 $(B)/mk09.img

# the same system for a console in EUC-JP and in Shift_JIS (jwp and its dictionary depend on the character set)
charsets:
	$(MAKE) B=build_euc CHARSET=euc
	$(MAKE) B=build_sjis CHARSET=sjis

# the host test of the file system code (random operations against a model; the volume is checked with fsck.fat)
$(B)/fs_host: tests/fs_host.c servers/fs_core.c servers/fs_core.h
	gcc -DMK_HOST -O1 -Wall -Wno-unused -Iinclude -Iservers -o $@ tests/fs_host.c servers/fs_core.c

fstest: $(B)/fs_host $(B)/mk09.img
	for seed in 1 2 3; do cp $(B)/mk09.img $(B)/fs_t.img; $(B)/fs_host $(B)/fs_t.img random $$seed 500 || exit 1; \
	  dd if=$(B)/fs_t.img of=$(B)/fs_v.img bs=512 skip=2048 2>/dev/null; fsck.fat -n $(B)/fs_v.img >/dev/null || exit 1; done; \
	rm -f $(B)/fs_t.img $(B)/fs_v.img; \
	for cfg in "1 30000 0" "16 300000 40" "64 600000 80"; do set -- $$cfg; rm -f $(B)/fs_b.img; mkfs.fat -F 16 -S 512 -s $$1 -C $(B)/fs_b.img $$2 >/dev/null || exit 1; \
	  $(B)/fs_host $(B)/fs_b.img random 4 400 || exit 1; [ $$3 = 0 ] || $(B)/fs_host $(B)/fs_b.img big $$3 || exit 1; fsck.fat -n $(B)/fs_b.img >/dev/null || exit 1; done; \
	rm -f $(B)/fs_b.img; echo "fstest: ok"

test: all charsets fstest
	sh tests/portcheck.sh
	gcc -O1 -o $(B)/sfp_test tests/sfp_test.c -lm && $(B)/sfp_test | tail -1
	gcc -O0 -w -DFP_HOST -o $(B)/fp_test tests/fp_host_test.c -lm && $(B)/fp_test | tail -1
	gcc -O0 -w -DFP_HOST -o $(B)/fpio_test tests/fpio_host_test.c -lm && $(B)/fpio_test | tail -1
	$(PYTHON) tests/pas_test.py
	$(PYTHON) tests/mk09_test.py
	$(PYTHON) tests/jwp_test.py
	$(PYTHON) tests/fpga_mk09.py

clean:
	rm -rf build build_euc build_sjis

.PHONY: all run clean charsets test fstest
