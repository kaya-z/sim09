* rt.s - the run time of the C compiler of MK09 (docs/IR.md): arithmetic helpers and the system call stubs.  Assembled by `as` together with the program.
* Each routine is a block that starts with a line  ;@def name  (the driver cc takes the blocks that the program needs).
*
* Helpers (called by the code of cg): D = left operand, X = right operand; the result is in D (div: quotient;  mod: the remainder is in X).

;@def __mul
__mul	pshs	x,d		; ,s = a (hi lo), 2,s = b (hi lo)
	lda	1,s
	ldb	3,s
	mul			; al * bl
	tfr	d,x
	lda	,s
	ldb	3,s
	mul			; ah * bl (the low byte counts)
	stb	,s
	lda	1,s
	ldb	2,s
	mul			; al * bh
	addb	,s
	stb	,s
	tfr	x,d
	adda	,s
	leas	4,s
	rts

;@def __udiv
;@def __umod
__udiv
__umod	cmpx	#0		; D / X unsigned: D = quotient, X = remainder (both 0 for a division by 0)
	bne	@ok
	clra
	clrb
	rts
@ok	pshs	y,x		; ,s = the divisor
	ldy	#16
	ldx	#0		; the remainder
@lp	aslb			; the next bit of the dividend
	rola
	exg	d,x		; D = remainder, X = the rest of the dividend
	rolb
	rola
	bcs	@sub
	cmpd	,s
	blo	@no
@sub	subd	,s
	exg	d,x
	orb	#1
	bra	@nx
@no	exg	d,x
@nx	leay	-1,y
	bne	@lp
	leas	2,s
	puls	y,pc

;@def __div
__div	pshs	x,d		; 0,s = a, 2,s = b
	lda	,s
	eora	2,s
	pshs	a		; the sign of the quotient
	ldd	3,s
	bpl	@b
	coma
	comb
	addd	#1
@b	tfr	d,x
	ldd	1,s
	bpl	@a
	coma
	comb
	addd	#1
@a	lbsr	__udiv
	tst	,s
	bpl	@r
	coma
	comb
	addd	#1
@r	leas	5,s
	rts

;@def __mod
__mod	pshs	x,d		; 0,s = a, 2,s = b
	ldd	2,s
	bpl	@b
	coma
	comb
	addd	#1
@b	tfr	d,x
	ldd	,s
	bpl	@a
	coma
	comb
	addd	#1
@a	lbsr	__udiv
	tst	,s		; the remainder has the sign of the dividend
	bpl	@r
	tfr	x,d
	coma
	comb
	addd	#1
	tfr	d,x
@r	leas	4,s
	rts

;@def __shl
__shl	cmpx	#16
	blo	@s
	ldx	#16
@s	cmpx	#0
	beq	@d
@l	aslb
	rola
	leax	-1,x
	bne	@l
@d	rts

;@def __shr
__shr	cmpx	#16
	blo	@s
	ldx	#16
@s	cmpx	#0
	beq	@d
@l	asra
	rorb
	leax	-1,x
	bne	@l
@d	rts

;@def __ushr
__ushr	cmpx	#16
	blo	@s
	ldx	#16
@s	cmpx	#0
	beq	@d
@l	lsra
	rorb
	leax	-1,x
	bne	@l
@d	rts

;@def __copy
__copy	pshs	u		; D bytes from X to Y
	tfr	d,u
@l	lda	,x+
	sta	,y+
	leau	-1,u
	cmpu	#0
	bne	@l
	puls	u,pc

* the system calls (the arguments are on the stack: the first one nearest to the top)
;@def _mk_call
_mk_call	ldb	3,s
	ldx	4,s
	lda	#3
	swi
	clra
	rts

;@def _mk_callg
_mk_callg	ldb	3,s
	ldx	4,s
	ldy	6,s
	lda	#4
	swi
	clra
	rts

;@def _mk_exit
_mk_exit	ldb	3,s
	lda	#12
	swi
	bra	_mk_exit
