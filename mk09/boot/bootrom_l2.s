* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* bootrom_l2.s - MK09 boot ROM image of the machine with the MMU (sim09 -2 puts the 8KB file at $7E000: the logical $E000-$FFFF of the system map).
* The code is at $FE00-$FF7F (the fixed page; the I/O registers are at $FF80), the hardware vectors at $FFF2.  Same protocol as bootrom.s; the system map is set first.
*
* Loads the system image from drive 0 (SD card), 256 byte sectors starting at sector 2 (the
* first 512 byte block holds the partition table), and jumps to it.  The image is a sequence
* of 256 byte sectors
*
*	[dest hi][dest lo][n][n bytes]		copy the n bytes to dest
*	[entry hi][entry lo][0]			the last sector: jump to the entry
*	[lsn hi][lsn lo][$FF]			continue with the sector lsn (the image goes on there; the n of a
*						record is at most 253, so $FF is free: the OS-9 kernel image of tools/mkkernel.py)
*
* (tools/mkimage.py).  This is the same protocol as fuzix/bootrom.s, so a machine (or an FPGA
* bitstream) with that ROM boots MK09 as well.  The hardware vectors go to the jump table
* that the kernel makes at $0100.

BUF	equ	$0200			sector buffer; no image data goes here
FIRSTLSN equ	2

	org	$E000
	rmb	$FE00-*			(the file is the whole block 7: the code is in the fixed page)

start	orcc	#$50			interrupts off
	clr	$FF91			the system map is in force
	ldx	#$FFA0			and it is the identity for the blocks 0-6 (the block 7 stays on the top of the RAM: the vectors)
	clra
map	sta	,x+
	inca
	cmpa	#7
	blo	map
	lds	#$0100
	ldx	#FIRSTLSN
	clr	$FFC1			drive 0
	clr	$FFC9			the disk synchronous (after a restart without a reset the OS may have left the asynchronous mode on)
next	clr	$FFC2
	stx	$FFC3			sector number (low 16 bits)
	ldd	#BUF
	std	$FFC5			DMA address
	lda	#$81
	sta	$FFC0			read
	tst	$FFC0			result: 0 = good
	bne	fail
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
run	lda	#$A4		; tell the board: the OS starts (LED4)
	sta	$FF8F
	jmp	,y
goto	ldx	BUF
	bra	next
fail	lda	#$A5			no boot disk: power off
	sta	$FF8F
	bra	fail

	rmb	$FFF0-*			padding up to the vectors
	fdb	0			$FFF0 reserved
	fdb	$0100			$FFF2 SWI3
	fdb	$0103			$FFF4 SWI2
	fdb	$010F			$FFF6 FIRQ
	fdb	$010C			$FFF8 IRQ
	fdb	$0106			$FFFA SWI (system calls)
	fdb	$0109			$FFFC NMI
	fdb	start			$FFFE RESET
