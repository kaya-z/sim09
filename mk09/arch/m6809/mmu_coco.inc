* mmu_coco.inc - the MMU of the sbc09 machine with the MMU (CoCo like: sim09 -2, the l2 mode of the FPGA; included by kernel.s under MMU and COCO).
*
* Eight blocks of 8KB.  There are two maps only: the system map ($FFA0-$FFA7: the servers and the idle task) and the user map ($FFA8-$FFAF: the program
* that runs), chosen by bit 0 of the task register $FF91.  The registers are written by software (an interrupt does not change the map).  So
*   - the servers run in the system map, which is the identity (the logical block n is the physical block n) for the blocks 0-6,
*   - the user map is loaded from T_MAP of the task each time a program is dispatched (mmu_select),
*   - the block 0 is the kernel in every map (the kernel code and data, the ring of the console, the vector table), and $FE00-$FFFF is the fixed page,
*   - the block 7 ($E000-$FDFF) is the window: the kernel puts a physical block of another task there (xcopy).
* Same calls as mmu_fm11.inc (see there): mmuinit, mmu_select, mmu_newmap, xcopy.
CO_TASK	equ	$FF91			bit 0: 0 = the system map, 1 = the user map
CO_SYS	equ	$FFA0			the registers of the system map (the physical block of the logical block 0-7)
CO_USR	equ	$FFA8			the registers of the user map
WINLOG	equ	$E000			the window (the logical block 7)
NWIN	equ	7

* mmuinit: the system map is the identity for the blocks 0-6; the entries T_MAP of the servers (tasks 0-5) too (the memory of a server is reached through the
* window by its physical block).  The map in force is the system one.
mmuinit	clr	CO_TASK
	ldx	#CO_SYS
	ldu	#CO_USR
	clra
@m	sta	,x+
	sta	,u+
	inca
	cmpa	#NWIN
	blo	@m
	clrb				(block 7: the window; it shows the physical block 7 until it is used)
	lda	#NWIN
	sta	CO_SYS+NWIN
	sta	CO_USR+NWIN
	clrb
@t	pshs	b
	lda	#TSZ
	mul
	addd	#tasks+T_MAP
	tfr	d,u
	clra
@p	sta	,u+
	inca
	cmpa	#NPG
	blo	@p
	puls	b
	incb
	cmpb	#TID_MM+1
	blo	@t
	rts

* mmu_select: Y = the task that runs (<cur): a server runs in the system map; a program in the user map, which is loaded from its T_MAP
mmu_select
	lda	<cur
	cmpa	#TID_USER0
	blo	@sys
	leax	T_MAP,y
	ldu	#CO_USR
	ldb	#NPG
@l	lda	,x+
	sta	,u+
	decb
	bne	@l
	lda	#1
	sta	CO_TASK
	rts
@sys	clr	CO_TASK
	rts

* mmu_newmap: X = the entry of a new task, its map in tcb+11 ..: the block 0 is the kernel in every map; the map goes into T_MAP (it is loaded when the program is
* dispatched)
mmu_newmap
	clr	tcb+11
	leau	T_MAP,x
	ldy	#tcb+11
	ldb	#NPG
@mp	lda	,y+
	sta	,u+
	decb
	bne	@mp
	rts

* xcopy: copy xn bytes between the caller's memory (xc) and the memory of the task xp (xa, a logical address in its map): xm = GR_READ: from the
* other task, GR_WRITE: to it.  The other memory is reached through the window (the block 7 of the map in force), at most to the end of a block at a time.
xcopy	ldd	xn
	lbeq	@done
	lda	xa			the block of the other task's address
	lsra
	lsra
	lsra
	lsra
	lsra
	ldx	xp
	leax	T_MAP,x
	lda	a,x			A = its physical block
	ldx	#CO_SYS+NWIN		the window register of the map in force
	ldb	CO_TASK
	andb	#1
	beq	@w
	ldx	#CO_USR+NWIN
@w	ldb	,x			(what it was)
	pshs	b,x
	sta	,x			the window shows the block
	ldd	xa
	anda	#$1F
	ora	#WINLOG/256		the address in the window
	std	xbuf			(xp is needed again: the address in the window is kept in xbuf)
	ldd	xa
	anda	#$1F
	pshs	d			(the offset in the block)
	ldd	#$2000
	subd	,s++			the bytes left in the block
	cmpd	xn
	bls	@ok
	ldd	xn
@ok	pshs	d			(the count)
	lda	xm
	cmpa	#GR_WRITE
	beq	@wr
	ldx	xbuf			from the other task: the window to the caller's memory
	ldu	xc
	ldd	,s
	lbsr	copy
	bra	@adv
@wr	ldx	xc			to the other task: the caller's memory to the window
	ldu	xbuf
	ldd	,s
	lbsr	copy
@adv	puls	d			the count
	std	<tmpw2
	ldb	,s+
	ldx	,s++
	stb	,x			(the window as it was)
	ldd	<tmpw2
	pshs	d
	addd	xa
	std	xa
	ldd	,s
	addd	xc
	std	xc
	ldd	xn
	subd	,s++
	std	xn
	lbra	xcopy
@done	rts
