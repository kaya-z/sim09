* rt9.s - the system calls of OS-9 for the C library of the OS-9 build (clib.c with -DOS9), docs/OS9.md.  Assembled by `as` like rt.s.
* The arguments are on the stack (16 bit each, the first one nearest to the top, as in rt.s).  The result is in D: >= 0, or -error (the error code of OS-9, 1..255).
* Each routine is a block that starts with a line  ;@def name.   U and Y are kept.

;@def _os9open
_os9open	pshs	u,y		; int os9open(char *path, int mode) -> the path number
	ldx	6,s
	lda	9,s
	swi2
	fcb	$84		; I$Open
	bcs	@e
	tfr	a,b
	clra
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9create
_os9create	pshs	u,y		; int os9create(char *path, int mode, int attr)
	ldx	6,s
	lda	9,s
	ldb	11,s
	swi2
	fcb	$83		; I$Create
	bcs	@e
	tfr	a,b
	clra
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9close
_os9close	pshs	u,y		; int os9close(int path)
	lda	7,s
	swi2
	fcb	$8F		; I$Close
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9read
_os9read	pshs	u,y		; int os9read(int path, char *buf, unsigned n) -> the number of bytes
	lda	7,s
	ldx	8,s
	ldy	10,s
	swi2
	fcb	$89		; I$Read
	bcs	@e
	tfr	y,d
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9write
_os9write	pshs	u,y		; int os9write(int path, char *buf, unsigned n)
	lda	7,s
	ldx	8,s
	ldy	10,s
	swi2
	fcb	$8A		; I$Write
	bcs	@e
	tfr	y,d
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9readln
_os9readln	pshs	u,y		; int os9readln(int path, char *buf, unsigned n): up to the CR
	lda	7,s
	ldx	8,s
	ldy	10,s
	swi2
	fcb	$8B		; I$ReadLn
	bcs	@e
	tfr	y,d
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9writln
_os9writln	pshs	u,y		; int os9writln(int path, char *buf, unsigned n): up to the CR (the terminal adds LF)
	lda	7,s
	ldx	8,s
	ldy	10,s
	swi2
	fcb	$8C		; I$WritLn
	bcs	@e
	tfr	y,d
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9seek
_os9seek	pshs	u,y		; int os9seek(int path, unsigned hi, unsigned lo)
	lda	7,s
	ldx	8,s
	ldu	10,s
	swi2
	fcb	$88		; I$Seek
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9stat
_os9stat	pshs	u,y		; int os9stat(int path, int code, unsigned *hi) -> the low word of the answer (SS.Size 2, SS.Pos 5), *hi = the high word
	lda	7,s
	ldb	9,s
	swi2
	fcb	$8D		; I$GetStt
	bcs	@e
	ldy	10,s
	stx	,y
	tfr	u,d
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9delete
_os9delete	pshs	u,y		; int os9delete(char *path)
	ldx	6,s
	swi2
	fcb	$87		; I$Delete
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9mkdir
_os9mkdir	pshs	u,y		; int os9mkdir(char *path, int attr)
	ldx	6,s
	ldb	9,s
	swi2
	fcb	$85		; I$MakDir
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9chdir
_os9chdir	pshs	u,y		; int os9chdir(char *path, int mode)
	ldx	6,s
	lda	9,s
	swi2
	fcb	$86		; I$ChgDir
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9time
_os9time	pshs	u,y		; int os9time(char *six_bytes): year-1900 (as OS-9), month, day, hour, minute, second
	ldx	6,s
	swi2
	fcb	$15		; F$Time
	bcs	@e
	clra
	clrb
	puls	u,y,pc
@e	clra
	coma
	comb
	addd	#1
	puls	u,y,pc

;@def _os9exit
_os9exit	ldb	3,s		; void os9exit(int code)
	swi2
	fcb	6		; F$Exit
	bra	_os9exit
