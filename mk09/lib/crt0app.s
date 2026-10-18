* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* crt0app.s - start of an MK09 program (an MX executable, version 2: position independent)
*
* The file starts with this 16 byte header (read by PM):
*	+0  'M' 'K'	magic
*	+2  2		version of the format (MX_VERSION of version.h)
*	+3  flags	bit 0: reserved
*	+4  load address the program was linked at
*	+6  entry address (_start) it was linked at
*	+8  size of the image (header, code, rodata, initialised data); the relocation table follows it
*	    (filled in by tools/mkmx.py)
*	+10 size of the bss in bytes
*	+12 stack size in bytes (STACKSZ, assemble with -DSTACKSZ=n)
*	+14 reserved
*
* The program can be loaded at any address that is a multiple of 256 (PM, servers/mm.c, picks it).
* tools/mkmx.py links the program at two addresses 256 bytes apart and finds all the places that
* hold the high byte of an address; their positions are the relocation table behind the image
* (gaps from the previous place: 1..254 = patch there, 255 = skip 254 bytes without patching,
* 0 = end).  The start code below, which is position independent, adds the distance between the
* address it was linked at and the address it runs at to those bytes, and then clears the bss -
* the table is where the bss begins, so it is overwritten and costs no memory.
* PM puts the stack and the start-up block (arguments) at the end of the program's memory and
* starts the program at the entry with X = address of the start-up block.

	include sysnum.inc
	include version.inc

	ifndef STACKSZ
STACKSZ	equ	512
	endc
	ifndef MXFLAGS
MXFLAGS	equ	0
	endc

	section start
	import	_crt_start
	import	l_bss
	import	s_bss
	import	s_start
	import	INITGL
	export	_start
	fcc	"MK"
	fcb	MX_VERSION
	fcb	MXFLAGS
	fdb	s_start
	fdb	_start
	fdb	0
	fdb	l_bss
	fdb	STACKSZ
	fdb	0
_start
	pshs	x			start-up block
	leax	_start,pcr		where we are (position independent: no absolute address up to the end of the loop)
	leay	-16,x			Y = the header
	tfr	x,d
	subd	6,y			D = the distance to the address we were linked at (a multiple of 256)
	beq	@norel
	pshs	a
	ldd	8,y			size of the image: the relocation table follows
	leau	d,y
	leax	,y			X = the place that is looked at
@nx	ldb	,u+
	beq	@dn
	abx
	cmpb	#255
	bne	@pt
	leax	-1,x			255: 254 bytes on without patching
	bra	@nx
@pt	lda	,x
	adda	,s
	sta	,x
	bra	@nx
@dn	leas	1,s
@norel	ldx	#s_bss
	ldd	#l_bss
	beq	@go
@clr	clr	,x+
	subd	#1
	bne	@clr
@go	lbsr	INITGL
	lbsr	_crt_start		never returns (exit)
	lda	#SYS_EXIT
	swi
	bra	*
	endsection

	section initgl_start
INITGL	export
INITGL	equ	*
	endsection
	section initgl_end
	rts
	endsection
