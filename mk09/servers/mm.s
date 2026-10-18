* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* mm.s - the memory manager (part of PM, see mm.h): a bitmap of the pages of the transient program area
*
* Programs are allocated from the bottom of the area, the shell from the top, so that the free
* memory stays in one piece in the middle (like the command processor of CP/M, which sat at the
* top of the memory).  There is no other state: the caller (pm.c) remembers what it was given.
* (In assembler because the C version needed 400 bytes, and the high memory in which PM lives is full.)
* The C interface is mm.h; CMOC passes every argument as a 16 bit word, the first nearest, and the
* result of an int function is in D.  The work variables are static (PM is one task, no re-entrancy).

	include memmap.inc

NP	equ	(TPA_END-TPA_BASE)/256		pages

	export	_mm_init
	export	_mm_alloc
	export	_mm_free
	export	_mm_info

	section bss
used	rmb	(NP+7)/8
m_n	rmb	1
m_run	rmb	1
m_i	rmb	1
m_first	rmb	1
m_free	rmb	1
m_big	rmb	1

	section code

* A = page number -> X = address of its byte, B = its bit mask   (clobbers A)
bitx	pshs	a
	lsra
	lsra
	lsra
	ldx	#used
	leax	a,x
	puls	a
	anda	#7
	ldb	#1
bx1	tsta
	beq	bx2
	aslb
	deca
	bra	bx1
bx2	rts

* test page A: Z = 1 if it is free
tstpg	bsr	bitx
	bitb	,x
	rts

markpg	bsr	bitx
	orb	,x
	stb	,x
	rts

clrpg	bsr	bitx
	comb
	andb	,x
	stb	,x
	rts

* void mm_init(void)
_mm_init
	ldx	#used
	ldb	#(NP+7)/8
mi0	clr	,x+
	decb
	bne	mi0
	rts

* int mm_alloc(u8 n, u8 top): the first page of n free pages in a row, or -1
_mm_alloc
	ldb	3,s
	beq	fail
	stb	m_n
	clr	m_run
	tst	5,s
	bne	fromtop
	clr	m_i			from the bottom
bu	lda	m_i
	cmpa	#NP
	bhs	fail
	lbsr	tstpg
	bne	bu_used
	inc	m_run
	lda	m_run
	cmpa	m_n
	bne	bu_next
	lda	m_i			found: the first page is  i - n + 1
	suba	m_n
	inca
	bra	claim
bu_used	clr	m_run
bu_next	inc	m_i
	bra	bu
fromtop	lda	#NP-1			from the top
	sta	m_i
td	lda	m_i
	lbsr	tstpg
	bne	td_used
	inc	m_run
	lda	m_run
	cmpa	m_n
	bne	td_next
	lda	m_i			found: the first page is  i
	bra	claim
td_used	clr	m_run
td_next	lda	m_i
	beq	fail
	dec	m_i
	bra	td
claim	sta	m_first			mark the n pages
	sta	m_i
	ldb	m_n
	stb	m_run
cl1	lda	m_i
	lbsr	markpg
	inc	m_i
	dec	m_run
	bne	cl1
	clra
	ldb	m_first
	rts
fail	ldd	#$FFFF
	rts

* void mm_free(u8 first, u8 n)
_mm_free
	lda	3,s
	sta	m_i
	ldb	5,s
	stb	m_run
	beq	fr9
fr1	lda	m_i
	lbsr	clrpg
	inc	m_i
	dec	m_run
	bne	fr1
fr9	rts

* void mm_info(u8 *total, u8 *free, u8 *largest)
_mm_info
	clr	m_free
	clr	m_run
	clr	m_big
	clr	m_i
mn1	lda	m_i
	lbsr	tstpg
	bne	mn_used
	inc	m_free
	inc	m_run
	lda	m_run
	cmpa	m_big
	bls	mn_next
	sta	m_big
	bra	mn_next
mn_used	clr	m_run
mn_next	inc	m_i
	lda	m_i
	cmpa	#NP
	blo	mn1
	ldx	2,s
	lda	#NP
	sta	,x
	ldx	4,s
	lda	m_free
	sta	,x
	ldx	6,s
	lda	m_big
	sta	,x
	rts
