* SPDX-License-Identifier: MIT
* Copyright (c) 2026 YUKARI Semiconductor Devices
* l2loader.s - the loader of the Level 2 machine of the FPGA board, for MK09 (the same job as os9/level2/l2boot.asm)
*
* The 256 byte boot ROM of the bitstream (the one of FUZIX) loads this code to $0300 and jumps to it (it is the record at LSN 2 of the card: tools/mkdisk.py --l2card).
* It writes $A2 to the mode register ($E00E): the machine becomes the Level 2 machine (the MMU, 2 MB, the I/O registers at $FF80); the system map is the identity
* for the blocks 0-6, so this code goes on.  It reads the ROM image of the MK09 Level 2 (boot/bootrom_l2.s, 8KB = 32 sectors from LSN 8) through the window
* $2000-$3FFF (the system map 1) into the physical block $3F ($7E000: the logical $E000-$FFFF of the system map), writes $A3 to the mode register (the CPU is reset in
* the Level 2 mode) and the ROM image starts: it sets the map, reads the system from LSN 64 and runs it.  Without a card it powers the machine off ($FF8F).

IMGLSN	equ	8
NSECT	equ	32

	org	$0300

start	orcc	#$50
	lda	#$A2
	sta	$E00E			Level 2 mode: from now on the registers are at $FF80
	ldx	#IMGLSN
	ldy	#NSECT
	lda	#$3F
	sta	$FFA1			the system map 1 = the window $2000-$3FFF: the physical block $3F
	lda	#$20			the buffer address (high byte)
next	clr	$FFC1			drive 0
	clr	$FFC2			LSN bits 23-16
	stx	$FFC3			LSN bits 15-0
	sta	$FFC5			the DMA address, a logical address: it goes through the map
	clr	$FFC6
	ldb	#$81
	stb	$FFC0			read the sector (the CPU waits)
	ldb	$FFC0			the result: 0 = good
	bne	fail
	leax	1,x
	leay	-1,y
	beq	done
	inca				the next 256 bytes of the window
	bra	next
done	lda	#$A3
	sta	$FF8E			reset the CPU: it starts the Level 2 machine
hang	bra	hang
fail	lda	#$A5
	sta	$FF8F			no card: power off
	bra	fail
