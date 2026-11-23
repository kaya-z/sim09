* mmu_fm11.inc - the memory management unit of the FM-11 Level 2 (included by kernel.s under MMU): 16 map sets of 16 pages of 4KB, a set for every task
* (the task number is the number of the set; the registers FM_MMR are those of the set that FM_TASK has selected), and a window at $7C00.
*
* What the kernel needs from a MMU (docs/V2_PLAN.md, stage 3; the CoCo like MMU of the sbc09 has its own file):
*   mmuinit      the maps of the idle task and the servers; the MMU on; the installed RAM tried
*   mmu_select   Y = the task that runs now: its map is the one in force
*   mmu_newmap   X = the entry of a new task: its map (tcb+11 .. tcb+11+NPG-1, physical pages) goes into T_MAP and into its map registers
*   xcopy        copy between the caller's memory (xc) and the memory of another task (xp, xa) through the window, xn bytes, xm = GR_READ / GR_WRITE
NSETS	equ	16
* (the equates of the registers are in kernel.s)
* mmuinit: the map sets of the idle task and of the servers (tasks 0-5) are the identity: the flat memory of Level 1.  Then the MMU is switched on.
* (the set of the disk server also has the 3 pages of its track buffer: logical $9000-$BFFF = the physical pages BOUNCE..)
mmuinit	clrb				every set maps the pages 0 and 1 (the kernel) as they are
@all	stb	FM_TASK
	clr	FM_MMR
	lda	#1
	sta	FM_MMR+1
	incb
	cmpb	#NSETS
	blo	@all
	clrb
@set	stb	FM_TASK
	pshs	b
	lda	#TSZ
	mul
	addd	#tasks+T_MAP
	tfr	d,u
	ldx	#FM_MMR
	clra
@pg	sta	,x+
	sta	,u+
	inca
	cmpa	#NPG
	bne	@pg
	puls	b
	cmpb	#TID_DISK
	bne	@nd
	lda	#BOUNCE
	sta	FM_MMR+9
	inca
	sta	FM_MMR+10
	inca
	sta	FM_MMR+11
@nd	incb
	cmpb	#TID_MM+1
	blo	@set
	clr	FM_TASK
	lda	#$81
	sta	FM_MCFG
	lbsr	memprobe
	rts

* memprobe: how much RAM the machine has.  The installed memory comes in steps of 128KB (32 pages of 4KB; the first step is always there).  For every
* further step the first two bytes of its first page are tried through the window (write $A5 $5A, read back); the step is there when they stay,
* and it is not an echo (a mirror of a lower step: the same bytes show in the first page of a step below).  The bytes are put back.  The number of
* the steps (1..8) is left at MEMGRP (K_DATA + K_DSZ - $91, behind the data of the kernel and in front of the ring of the console), for MM.
MEMGRP	equ	K_DATA+K_DSZ-$91
memprobe lda	#1
	pshs	a			g: the step that is tried (its first page is 32 * g)
@g	lda	,s
	asla
	sta	FM_WIN1			physical address bits 19-16 of the page 32 * g
	lda	#$100-WINLOG/256	(bits 15-8 = 0)
	sta	FM_WIN2
	lda	#$C1
	sta	FM_MCFG			window on
	ldx	#WINLOG
	ldd	,x
	pshs	d			(what was there)
	lda	#$A5
	ldb	#$5A
	std	,x
	ldd	,x
	cmpd	#$A55A
	bne	@no
	clr	,-s			h: the steps below
@h	lda	,s
	cmpa	3,s
	bhs	@ok
	asla
	sta	FM_WIN1
	ldd	,x
	cmpd	#$A55A
	beq	@echo
	inc	,s
	bra	@h
@echo	leas	1,s
	bra	@no
@ok	leas	1,s
	lda	2,s
	asla
	sta	FM_WIN1
	puls	d
	std	,x			(put back)
	puls	a
	inca
	cmpa	#8
	bhs	@all
	pshs	a
	bra	@g
@all	lda	#8
	bra	@end
@no	lda	2,s
	asla
	sta	FM_WIN1
	puls	d
	std	,x			(put back)
	puls	a
@end	sta	MEMGRP
	lda	#$81
	sta	FM_MCFG			window off
	rts

* xcopy: copy xn bytes between the caller's memory (xc) and the memory of the task xp (xa, a logical address in its map): xm = GR_READ: from the
* other task, GR_WRITE: to it.  The other memory is reached through the window at $7C00 (a physical address chosen with FM_WIN1 / FM_WIN2),
* 256 bytes at most at a time (never across a page of the other map), through xbuf.
xcopy	ldd	xn
	lbeq	@done
	lda	xa			the logical page of the other task's address
	lsra
	lsra
	lsra
	lsra
	ldx	xp
	leax	T_MAP,x
	lda	a,x			A = its physical page
	tfr	a,b
	lsra
	lsra
	lsra
	lsra
	sta	FM_WIN1			physical address bits 19-16
	andb	#$0F
	aslb
	aslb
	aslb
	aslb				B = (page & 15) << 4
	lda	xa
	anda	#$0F
	pshs	a
	orb	,s+			B = bits 15-8 of the physical address
	subb	#WINLOG/256		the window shows FM_WIN2 * 256 + $7C00 + n
	stb	FM_WIN2
	ldb	xa+1			B = the offset in the 256 byte block
	clra
	pshs	d			(offset)
	negb
	bne	@nz			256 - offset
	ldd	#256
	bra	@l1
@nz	clra
@l1	cmpd	xn
	bls	@ok
	ldd	xn
@ok	pshs	d			(count, offset below)
	lda	xm
	cmpa	#GR_WRITE
	beq	@wr
* from the other task: the window to xbuf, then xbuf to the caller's memory
	lda	#$C1
	sta	FM_MCFG			window on
	ldd	2,s
	addd	#WINLOG
	tfr	d,x
	ldu	#xbuf
	ldd	,s
	lbsr	copy
	lda	#$81
	sta	FM_MCFG			window off
	ldx	#xbuf
	ldu	xc
	ldd	,s
	lbsr	copy
	bra	@adv
* to the other task: the caller's memory to xbuf, then xbuf to the window
@wr	ldx	xc
	ldu	#xbuf
	ldd	,s
	lbsr	copy
	lda	#$C1
	sta	FM_MCFG			window on
	ldd	2,s
	addd	#WINLOG
	tfr	d,u
	ldx	#xbuf
	ldd	,s
	lbsr	copy
	lda	#$81
	sta	FM_MCFG			window off
@adv	puls	d			count
	leas	2,s
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

* mmu_select: the map set of the task that runs (<cur) is the one in force
mmu_select
	lda	<cur
	sta	FM_TASK
	rts

* mmu_newmap: X = the entry of a new task (<dstid), its map in tcb+11 ..: the pages 0 and 1 are the kernel in every map; the map goes into T_MAP of the entry and
* into the registers of the set of the task; then the map of the running task is selected again
mmu_newmap
	clr	tcb+11
	lda	#1
	sta	tcb+12
	leau	T_MAP,x
	ldy	#tcb+11
	ldb	#NPG
@mp	lda	,y+
	sta	,u+
	decb
	bne	@mp
	ldb	<dstid
	stb	FM_TASK
	ldx	#FM_MMR
	ldy	#tcb+11
	ldb	#NPG
@mr	lda	,y+
	sta	,x+
	decb
	bne	@mr
	lda	<cur
	sta	FM_TASK
	rts
