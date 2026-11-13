* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* ipl_fm11_hdd.s - MK09 IPL for the FM-11 hard disk (MDC)
*
* The boot ROM of the FM-11 reads cylinder 0, head 0, records 0 and 1 (512 bytes) of the hard disk to $0400 and jumps to
* $0400 with the interrupts off, the MMU off and the boot ROM still mapped at $5000-$5FFF.  This code (the first 446 bytes of the
* MBR of the disk, tools/mkdisk.py --ipl) switches the ROM off and loads the system image from the sectors
* 2.. of the disk (tools/mkimage.py: the same records as boot/bootrom.s of the sbc09 board), then jumps to its entry.
*
* Sector number lsn -> cylinder = lsn / 256, head = (lsn / 32) mod 8, record = lsn mod 32 (the drive type $0C: 8 heads, 32 records).
* No image data may go to the sector buffer $0200-$02FF or to $0400-$05FF (this code).

BUF	equ	$0200			sector buffer
PAM	equ	$03E0			the command parameters (8 bytes)
R0	equ	$03EC			result byte 0
LSN	equ	$03EE

MDCST	equ	$FDC0			status / command
MDCDAT	equ	$FDC1
MDCCTL	equ	$FDC2

	org	$0400

ipl	orcc	#$50
	lds	#$FC7F
	lda	#$01
	sta	$FD93			boot ROM off: RAM at $5000-$5FFF (the MMU stays off)
	clr	$FD02			no interrupt
	lda	#$08
	sta	MDCCTL			reset the controller
	ldx	#4000
@w	leax	-1,x
	bne	@w
	lda	#$64			DMA channel 2, no interrupt, unit 0
	sta	MDCCTL
	lda	$FD54			DMA: enable the channel, no interrupt of it
	ora	#$04
	sta	$FD54
	lda	$FD55
	anda	#$FB
	sta	$FD55
	lda	#$08
	sta	$FD56
	ldx	#PAM			recalibrate
	clr	,x+
	clr	,x
	lda	#$10
	ldx	#PAM
	bsr	mdccmd
	ldx	#2			the first sector of the image
next	stx	LSN
	tfr	x,d			A = lsn / 256 = cylinder, B = lsn mod 256
	sta	PAM+3
	clr	PAM
	clr	PAM+1
	clr	PAM+2
	tfr	b,a
	lsra
	lsra
	lsra
	lsra
	lsra
	sta	PAM+4			head
	andb	#$1F
	stb	PAM+5			record
	lda	#1
	sta	PAM+6			one sector
	clr	PAM+7
	lda	#2			DMA channel 2: BUF, 256 bytes, device to memory
	sta	$FD48
	clr	$FD49
	lda	#1
	sta	$FD4A
	clr	$FD4B
	clr	$FD96
	lda	#$04
	sta	$FD52
	lda	#$22
	ldx	#PAM
	bsr	mdccmd
	lda	R0
	anda	#$F0
	cmpa	#$10
	bne	fail
	ldx	LSN
	leax	1,x
	ldy	BUF			destination or entry
	ldb	BUF+2			byte count
	beq	run
	incb
	beq	goto			$FF: go on at the sector number in the record
	decb
	ldu	#BUF+3
copy	lda	,u+
	sta	,y+
	decb
	bne	copy
	bra	next
run	jmp	,y
goto	ldx	BUF
	bra	next

* mdccmd: A = command, X = 8 parameter bytes; R0 = result byte 0 (the other result bytes are read and dropped)
mdccmd	ldb	MDCST
	bpl	mdccmd			wait for READY
	sta	MDCST
@p	ldb	MDCST
	bitb	#$20			PREQ: the next parameter
	beq	@r
@pw	ldb	MDCST
	lsrb
	bcc	@pw			wait for DRQ
	lda	,x+
	sta	MDCDAT
	bra	@p
@r	ldb	MDCST
	bitb	#$40			RDMD: the result
	beq	@r
	clr	R0
	ldy	#R0
@rl	ldb	MDCST
	bitb	#$40
	beq	@rx
@rw	ldb	MDCST
	lsrb
	bcc	@rw
	lda	MDCDAT
	sta	,y
	cmpy	#R0
	bne	@rl			(only the first byte is kept)
	leay	1,y
	bra	@rl
@rx	rts

* the system image cannot be read: tell the display sub system (PUT STRING) and stop
fail	ldx	#msg
	lda	#msgend-msg
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
msg	fcc	"MK09: cannot read the system image"
msgend
