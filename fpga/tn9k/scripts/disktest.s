; SPDX-License-Identifier: MIT
; Copyright (c) 2026 sim09 contributors
;
; A test of the disk port in its asynchronous mode ($E049), for the Verilator simulation (ROM at $FC00).  The card image is made by
; tests/fpga_test.py: byte i of sector n is (n*37 + 3*i + 1) & $FF.  Prints one line for each test (PASS or FAIL), then DONE, and powers off.
;   1  a synchronous read (the old way) gives the sector
;   2  an asynchronous read: the command returns at once ($E040 = $FE), the CPU runs while the disk works, then the sector is there and the status says done
;   3  the CPU writes and reads back memory while the disk transfers (the PSRAM arbiter): no byte is lost or changed
;   4  the interrupt at the end of a command (IRQ), acknowledged by writing the status
;   5  an asynchronous write, read back (another block is read in between, so that the buffer of the controller does not give it back)

ACIA	equ $E000
DCMD	equ $E040
DDRV	equ $E041
DL2	equ $E042
DL1	equ $E043
DL0	equ $E044
DBH	equ $E045
DBL	equ $E046
DST	equ $E049
POWER	equ $E00F

FLAG	equ $10			; set by the interrupt handler
ERRS	equ $11			; errors of the test that runs
CNT	equ $12			; (2) iterations of the wait loop
SAVEB	equ $14

	org $FC00

start:	orcc #$50
	lds #$0100
	ldx #banner
	lbsr puts
	; ---- test 1: a synchronous read
	clr DST
	clr <ERRS
	ldb #3
	ldx #$2000
	lbsr rdcmd
	ldb #3
	ldx #$2000
	lbsr check
	ldx #n1
	lbsr verdict
	; ---- test 2: an asynchronous read
	lda #$01
	sta DST
	ldb #5
	ldx #$2100
	lbsr setup
	lda #$81
	sta DCMD
	clr <ERRS
	lda DCMD		; busy: $FE
	cmpa #$FE
	beq t2a
	inc <ERRS
t2a:	ldx #0
t2w:	leax 1,x
	lda DST
	bpl t2w			; until the done flag (bit 7)
	stx <CNT
	cmpx #8			; the CPU did run while the disk was busy
	bhs t2b
	inc <ERRS
t2b:	lda DCMD		; the result, and it takes the done flag
	bne t2c
	lda DST
	bpl t2d
t2c:	inc <ERRS
t2d:	ldb #5
	ldx #$2100
	lbsr check
	ldx #n2
	lbsr verdict
	; ---- test 3: the CPU hammers the memory while the disk transfers
	ldb #6
	ldx #$2200
	lbsr setup
	lda #$81
	sta DCMD
	clr <ERRS
t3l:	ldx #$3000		; write 256 bytes, read them back
	ldb #0
t3w:	stb ,x+
	incb
	bne t3w
	ldx #$3000
	ldb #0
t3r:	cmpb ,x+
	beq t3o
	inc <ERRS
t3o:	incb
	bne t3r
	lda DST
	bpl t3l
	lda DCMD
	ldb #6
	ldx #$2200
	lbsr check
	ldx #n3
	lbsr verdict
	; ---- test 4: the interrupt
	clr <FLAG
	lda #$81		; asynchronous, interrupt at the end
	sta DST
	ldb #7
	ldx #$2300
	lbsr setup
	lda #$81
	sta DCMD
	clr <ERRS
	andcc #$EF		; the interrupts on
	ldx #0
t4w:	leax 1,x
	tst <FLAG
	bne t4d
	cmpx #$F000
	bne t4w
	inc <ERRS		; time out
t4d:	orcc #$10
	lda DST			; the handler has switched the interrupt off; done is still set until the result is read
	bmi t4e
	inc <ERRS
t4e:	lda DCMD
	bne t4f
	ldb #7
	ldx #$2300
	lbsr check
	lda DST			; the result has been read: done is clear
	bpl t4g
t4f:	inc <ERRS
t4g:	ldx #n4
	lbsr verdict
	; ---- test 5: an asynchronous write and the read back
	lda #$01
	sta DST
	ldx #$2400		; the data of sector 21 into the buffer
	ldb #21
	lbsr fill
	ldb #20
	ldx #$2400
	lbsr setup
	lda #$55
	sta DCMD
t5w:	lda DST
	bpl t5w
	lda DCMD
	sta <SAVEB		; the result of the write
	clr DST			; synchronous again
	ldb #100		; another block
	ldx #$2500
	lbsr rdcmd
	ldb #20
	ldx #$2600
	lbsr rdcmd
	clr <ERRS
	lda <SAVEB
	beq t5a
	inc <ERRS
t5a:	ldb #21			; sector 20 must hold the data of sector 21
	ldx #$2600
	lbsr check
	ldx #n5
	lbsr verdict
	ldx #done
	lbsr puts
	lda #$A5
	sta POWER
halt:	bra halt

irq:	lda DST			; the handler: the interrupt off (bit 7 of the status written 0), the flag
	bpl irqx
	lda #$01
	sta DST
	inc <FLAG
irqx:	rti

; setup: B = sector number, X = the buffer
setup:	clr DDRV
	clr DL2
	clr DL1
	stb DL0
	tfr x,d
	sta DBH
	stb DBL
	rts

; rdcmd: a synchronous read of sector B into X
rdcmd:	lbsr setup
	lda #$81
	sta DCMD
	lda DCMD
	rts

; fill: the data of sector B (the test pattern) into X
fill:	lda #37
	mul			; D = B*37 (n < 256: B is the low byte)
	incb
	stb <SAVEB
	ldb #0
fl1:	lda <SAVEB
	sta ,x+
	adda #3
	sta <SAVEB
	incb
	bne fl1
	rts

; check: the 256 bytes at X must be the data of sector B; the errors are added to ERRS
check:	lda #37
	mul
	incb
	stb <SAVEB
	ldb #0
ck1:	lda <SAVEB
	cmpa ,x+
	beq ck2
	inc <ERRS
ck2:	adda #3
	sta <SAVEB
	incb
	bne ck1
	rts

; verdict: X = the name of the test; prints PASS or FAIL
verdict: lbsr puts
	tst <ERRS
	bne vf
	ldx #pass
	lbra puts
vf:	ldx #fail
	lbra puts

puts:	lda ,x+
	beq pse
	sta ACIA+1
	bra puts
pse:	rts

banner:	fcc "DISKTEST"
	fcb 13,10,0
n1:	fcc "1 sync read: "
	fcb 0
n2:	fcc "2 async read, CPU runs: "
	fcb 0
n3:	fcc "3 CPU and DMA share the memory: "
	fcb 0
n4:	fcc "4 interrupt: "
	fcb 0
n5:	fcc "5 async write: "
	fcb 0
pass:	fcc "PASS"
	fcb 13,10,0
fail:	fcc "FAIL"
	fcb 13,10,0
done:	fcc "DONE"
	fcb 13,10,0

	rmb $FFF8-*
	fdb irq
	fdb $FFFF
	fdb $FFFF
	fdb start
