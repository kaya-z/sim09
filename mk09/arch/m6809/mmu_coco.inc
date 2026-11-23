* mmu_coco.inc - the MMU of the sbc09 machine with the MMU (CoCo like: sim09 -2, the l2 mode of the FPGA; included by kernel.s under MMU and COCO).
*
* Eight blocks of 8KB.  There are two maps only: the system map ($FFA0-$FFA7: the servers and the idle task) and the user map ($FFA8-$FFAF: the program
* that runs), chosen by bit 0 of the task register $FF91.  The registers are written by software (an interrupt does not change the map).  So
*   - the servers run in the system map, which is the identity (the logical block n is the physical block n),
*   - the user map is loaded from T_MAP of the task each time a program is dispatched (mmu_select),
*   - the block 0 is the kernel in every map (the kernel code and data, the ring of the console, the vector table), and $FE00-$FFFF is the fixed page
*     (the vectors, the I/O registers): a program has the logical addresses $2000-$FDFF.
* There is no window onto the memory of another task: the kernel copies through xbuf (in the block 0, in every map) and selects the map of the other task
* for one half of the copy (xcopy).  Same calls as mmu_fm11.inc (see there): mmuinit, mmu_select, mmu_newmap, xcopy.
CO_TASK	equ	$FF91			bit 0: 0 = the system map, 1 = the user map
CO_SYS	equ	$FFA0			the registers of the system map (the physical block of the logical block 0-7)
CO_USR	equ	$FFA8			the registers of the user map
NBLK	equ	8

* mmuinit: the system map is the identity; the entries T_MAP of the servers (tasks 0-5) too.  The map in force is the system one.
mmuinit	clr	CO_TASK
	ldx	#CO_SYS
	ldu	#CO_USR
	clra
@m	sta	,x+
	sta	,u+
	inca
	cmpa	#NBLK
	blo	@m
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
mmu_map	cmpy	#tasks+TID_USER0*TSZ	(Y = the entry of a task: the map of the task is put in force)
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

* xcopy: copy xn bytes between the caller's memory (xc, in the map in force) and the memory of the task xp (xa, a logical address in its map): xm = GR_READ: from the
* other task, GR_WRITE: to it.  256 bytes at most at a time, through xbuf: the map of the other task is in force for one half of the copy.
xcopy	ldd	xn
	lbeq	@done
	cmpd	#256
	bls	@c
	ldd	#256
@c	pshs	d			(the bytes of this round)
	lda	xm
	cmpa	#GR_WRITE
	beq	@wr
	ldy	xp			from the other task: its memory to xbuf, then xbuf to the caller's memory
	lbsr	mmu_map
	ldx	xa
	ldu	#xbuf
	ldd	,s
	lbsr	copy
	ldy	<curp
	lbsr	mmu_map
	ldx	#xbuf
	ldu	xc
	ldd	,s
	lbsr	copy
	bra	@adv
@wr	ldx	xc			to the other task: the caller's memory to xbuf, then xbuf to its memory
	ldu	#xbuf
	ldd	,s
	lbsr	copy
	ldy	xp
	lbsr	mmu_map
	ldx	#xbuf
	ldu	xa
	ldd	,s
	lbsr	copy
	ldy	<curp
	lbsr	mmu_map
@adv	ldd	,s
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
