* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* ipl_fm11_2hd.s - MK09 IPL for the FM-11 2HD floppy disk (MB8877, drive 0)
*
* The boot ROM of the FM-11 reads the sectors 1-4 of track 0 side 0 of the 2HD disk (128 bytes each: 512 bytes) to $0400 and jumps to
* $0400 with the interrupts off, the MMU off and the boot ROM still mapped at $5000-$5FFF.  This code switches the ROM off and loads the
* system image from the 256 byte sectors that follow track 0: lsn 0 = track 0 side 1 sector 1, 26 sectors per side (see bsp/fm11/fdc.c);
* the image (tools/mkimage.py) starts at lsn 2.  Records as in boot/bootrom.s (without the continue record).
* No image data may go to the sector buffer $0200-$02FF or to $0400-$05FF.

BUF	equ	$0200
TRK	equ	$03F0			track of the next sector
SID	equ	$03F1			side
SEC	equ	$03F2			sector 1..26
CUR	equ	$03F3			track of the head
STG	equ	$03F4			stage: 1 restore, 2 seek, 3 read
DST	equ	$03F5
DIRQ	equ	$03F6
MSGB	equ	$03C0
D35A	equ	$03F7			$FD35 at the entry
RTY	equ	$03F8			retries left

FDCST	equ	$FD30
FDCTRK	equ	$FD31
FDCSEC	equ	$FD32
FDCDAT	equ	$FD33
FDCSID	equ	$FD34
DENS	equ	$40			$FD34 bit 6 (density) for the MFM tracks: variant A = 1
FDCDRV	equ	$FD35
FDCIRQ	equ	$FD37

	org	$0400

ipl	orcc	#$50
	lds	#$FC7F
	lda	#$01
	sta	$FD93			boot ROM off: RAM at $5000-$5FFF (the MMU stays off)
	clr	$FD02			no interrupt
	lda	FDCDRV
	sta	D35A
	anda	#$80			unit 0; bit 7 as the boot ROM left it (the ROM writes the unit number only)
	sta	FDCDRV
	lda	#10
	sta	RTY
again	lds	#$FC7F
	lda	$FD54			DMA channel 1: enable, no interrupt of it
	ora	#$02
	sta	$FD54
	lda	$FD55
	anda	#$FD
	sta	$FD55
	lda	#$08
	sta	$FD56
	lda	#$0A			restore: track 0
	sta	FDCST
	lda	#1
	sta	STG
	lbsr	wait
	clr	TRK
	clr	CUR
	lda	#1
	sta	SID
	lda	#3			lsn 2 = track 0 side 1 sector 3
	sta	SEC
next	lda	TRK
	cmpa	CUR
	beq	@sk
	sta	FDCDAT			seek to the track
	sta	CUR
	lda	#$1A
	sta	FDCST
	lda	#2
	sta	STG
	lbsr	wait
@sk	lda	SID
	ora	#DENS
	sta	FDCSID
	lda	SEC
	sta	FDCSEC
	clr	$FD57
	lda	#2			DMA channel 1: BUF, 256 bytes, device to memory
	sta	$FD44
	clr	$FD45
	lda	#1
	sta	$FD46
	clr	$FD47
	clr	$FD95
	lda	#$04
	sta	$FD51
	lda	#$80			read sector
	sta	FDCST
	lda	#3
	sta	STG
	lbsr	wait
	lda	FDCST
	anda	#$9C			not ready, record not found, CRC, lost data
	bne	fail
	inc	SEC			the next sector
	lda	SEC
	cmpa	#27
	bne	@rd
	lda	#1
	sta	SEC
	lda	SID
	eora	#1
	sta	SID
	bne	@rd
	inc	TRK
@rd	ldy	BUF			destination or entry
	ldb	BUF+2			byte count
	beq	run
	ldu	#BUF+3
copy	lda	,u+
	sta	,y+
	decb
	bne	copy
	lbra	next
run	jmp	,y

* wait: until the controller has finished the command (interrupt flag $FD37 bit 6)
wait	ldy	#4
@o	ldx	#0
@w	lda	FDCIRQ
	bita	#$40
	bne	@x
	leax	-1,x
	bne	@w
	leay	-1,y
	bne	@o
	bra	fail
@x	rts

* the system image cannot be read: tell the display sub system (PUT STRING) and stop
fail	lda	FDCIRQ
	sta	DIRQ
	lda	FDCST
	sta	DST
	lda	RTY
	beq	giveup
	deca
	sta	RTY
	lds	#$FC7F
	lda	#$D0			force interrupt, then try again from the start
	sta	FDCST
	ldx	#$4000
@d	leax	-1,x
	bne	@d
	lbra	again
giveup
	ldy	#MSGB
	ldx	#m1
	bsr	str
	lda	DST
	bsr	hex
	ldx	#m2
	bsr	str
	lda	DIRQ
	bsr	hex
	ldx	#m3
	bsr	str
	lda	STG
	bsr	hex
	ldx	#m4
	bsr	str
	lda	TRK
	bsr	hex
	ldx	#m5
	bsr	str
	lda	SID
	bsr	hex
	ldx	#m6
	bsr	str
	lda	SEC
	bsr	hex
	ldx	#m7
	bsr	str
	lda	D35A
	bsr	hex
	lda	FDCDRV
	bsr	hex
	tfr	y,d
	subd	#MSGB
	tfr	b,a
	ldx	#MSGB
@w1	ldb	$FD05
	bmi	@w1
	ldb	#$80
	stb	$FD05
@w2	ldb	$FD05
	bpl	@w2
	ldy	#$FC80
	clr	1,y
	ldb	#3
	stb	2,y
	sta	3,y
	leay	4,y
@cp	ldb	,x+
	stb	,y+
	deca
	bne	@cp
	clr	$FD05
	bra	*
str	lda	,x+
	beq	@e
	sta	,y+
	bra	str
@e	rts
hex	pshs	a
	lsra
	lsra
	lsra
	lsra
	bsr	nib
	puls	a
	anda	#$0F
nib	adda	#$30
	cmpa	#$39
	bls	@s
	adda	#7
@s	sta	,y+
	rts
m1	fcc	"MK09 ST="
	fcb	0
m2	fcc	" IRQ="
	fcb	0
m3	fcc	" G="
	fcb	0
m4	fcc	" T="
	fcb	0
m5	fcc	" H="
	fcb	0
m6	fcc	" R="
	fcb	0
m7	fcc	" D="
	fcb	0
