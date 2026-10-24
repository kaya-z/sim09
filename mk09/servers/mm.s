* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* mm.s - the memory manager (TASK 5, see mm.h): the books of the pages of the transient program area
*
* A server task like the others, but written in assembler: the C version needed 400 bytes of code and a C start-up, and
* the high memory in which the servers live is full.  PM is its client (MM_ALLOC and MM_FREE come from PM only; MM_INFO from
* anybody, protocol MM_* in proto.h).  A bitmap of the pages (256 bytes each) of TPA_BASE .. TPA_END.
*
* Programs are allocated from the bottom of the area, the shell from the top (PM says which), so that the free memory stays in
* one piece in the middle (like the command processor of CP/M, which sat at the top of the memory).  MM does not know who has
* which pages: PM remembers what it was given and gives it back.  The kernel protects the regions.
*
* The task starts with its stack at the end of its data slot (the kernel made the first frame).  It uses the system calls
* directly (A = number, B = task, X = message; see lib/mksys.s) and needs no C library.

	include memmap.inc
	include sysnum.inc

NP	equ	(TPA_END-TPA_BASE)/256		pages

TID_ANY	equ	$FF
TID_HW	equ	$FE
TID_PM	equ	4
E_OK	equ	0
E_PERM	equ	1
E_INVAL	equ	8
E_NOMEM	equ	6
E_NOSYS	equ	22
MM_ALLOC equ	1
MM_FREE	equ	2
MM_INFO	equ	3

	export	_start

	section start			(the kernel starts the task at the first byte of its code)
_start	lbra	init
	endsection

	section bss
used	rmb	(NP+7)/8
msg	rmb	10			src op a(2) b(2) c(2) d(2)
m_src	rmb	1
m_n	rmb	1
m_run	rmb	1
m_i	rmb	1
m_first	rmb	1
m_free	rmb	1
m_big	rmb	1
endbss	rmb	0

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

* alloc: B = n pages, A = 0 (from the bottom) or not (from the top) -> carry clear and B = the first page, or carry set
alloc	tstb
	beq	fail
	stb	m_n
	clr	m_run
	tsta
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
	ldb	m_first
	andcc	#$FE
	rts
fail	orcc	#1
	rts

* free: A = first page, B = n
free	sta	m_i
	stb	m_run
	beq	fr9
fr1	lda	m_i
	lbsr	clrpg
	inc	m_i
	dec	m_run
	bne	fr1
fr9	rts

* info: -> msg a = pages, b = free pages, c = the biggest piece of free pages
info	clr	m_free
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
	clr	msg+2
	lda	#NP
	sta	msg+3
	clr	msg+4
	lda	m_free
	sta	msg+5
	clr	msg+6
	lda	m_big
	sta	msg+7
	rts

* ---- the server
init	ldx	#used			clear the books
	ldb	#(NP+7)/8
st0	clr	,x+
	decb
	bne	st0
	ldx	#endbss			fill the unused stack with $A5 (the tool stk measures it)
	lda	#$A5
st1	sta	,x+
	cmpx	#MM_DATA+MM_DSZ-32
	blo	st1
mmloop	ldb	#TID_ANY
	ldx	#msg
	lda	#SYS_RECV
	swi
	tstb
	bne	mmloop
	lda	msg
	sta	m_src
	cmpa	#TID_HW
	beq	mmloop
	lda	msg+1
	cmpa	#MM_INFO
	beq	do_info
	cmpa	#MM_ALLOC
	beq	do_alloc
	cmpa	#MM_FREE
	beq	do_free
	ldb	#E_NOSYS
	bra	rep0
do_info	lbsr	info
	ldb	#E_OK
	bra	rep
do_alloc ldb	#E_PERM
	lda	m_src
	cmpa	#TID_PM
	bne	rep0
	ldb	#E_INVAL
	lda	msg+2			a = n must fit a byte
	bne	rep0
	lda	msg+9			d = from the top?
	ldb	msg+3
	lbsr	alloc
	bcs	nomem
	clr	msg+2
	stb	msg+3
	ldb	#E_OK
	bra	rep
nomem	ldb	#E_NOMEM
	bra	rep0
do_free	ldb	#E_PERM
	lda	m_src
	cmpa	#TID_PM
	bne	rep0
	lda	msg+3			first
	ldb	msg+5			n
	lbsr	free
	ldb	#E_OK
	bra	rep0
rep0	clr	msg+2			no values
	clr	msg+3
	clr	msg+4
	clr	msg+5
	clr	msg+6
	clr	msg+7
rep	stb	msg+1			the status
	clr	msg+8
	clr	msg+9
	ldb	m_src
	ldx	#msg
	lda	#SYS_REPLY
	swi
	lbra	mmloop
