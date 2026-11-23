; SPDX-License-Identifier: MIT
; Copyright (c) 2026 sim09 contributors
;
; A test of the second ACIA ($E004/05, the second UART): in the Verilator test bench its txd is wired to its rxd (a loop back).
; It sends 16 bytes through it and reads them back (status bit 0 = RDRF), checks that the control register $95 puts the interrupt
; request (status bit 7) up when a character has arrived and that the master reset ($03) takes it down.
; The result goes to the console ACIA ($E000):   ACIA1 PASS   or   ACIA1 FAIL <n>,   then the power is switched off ($E00F = $A5).

ACIA	equ $E000
ACIA1	equ $E004
POWER	equ $E00F
ERR	equ $10

	org $FC00

start:	orcc #$50
	lds #$0100
	clr <ERR
	ldx #banner
	lbsr puts
	ldb #$A0
send:	stb ACIA1+1			; write a byte
	ldx #0
wait:	lda ACIA1
	bita #1				; RDRF
	bne got
	leax -1,x
	bne wait
	inc <ERR			; timeout
	bra next
got:	lda ACIA1+1
	pshs b
	cmpa ,s+
	beq next
	inc <ERR			; a wrong byte
next:	incb
	cmpb #$B0
	bne send
	; the interrupt request: control $95 (RIE), send a byte, wait for RDRF, status bit 7
	lda #$95
	sta ACIA1
	lda #$5A
	sta ACIA1+1
	ldx #0
w2:	lda ACIA1
	bita #1
	bne g2
	leax -1,x
	bne w2
	inc <ERR
	bra z
g2:	lda ACIA1
	bmi irqok			; bit 7: the interrupt request
	inc <ERR
irqok:	lda #$03			; master reset: the request goes down
	sta ACIA1
	lda ACIA1
	bpl z
	inc <ERR
z:	lda ACIA1+1			; (take the byte)
	; the console ACIA must not be disturbed: the status of the console is still readable
	lda ACIA
	ldx #pass
	tst <ERR
	beq say
	ldx #fail
say:	lbsr puts
	lda <ERR
	adda #'0'
	lbsr putc
	lda #13
	lbsr putc
	lda #10
	lbsr putc
	lda #$A5
	sta POWER
hang:	bra hang

puts:	lda ,x+
	beq pdone
	lbsr putc
	bra puts
pdone:	rts

putc:	pshs a
pw:	lda ACIA
	bita #2				; TDRE
	beq pw
	puls a
	sta ACIA+1
	rts

banner:	fcc "ACIA1 test"
	fcb 13,10,0
pass:	fcc "ACIA1 PASS "
	fcb 0
fail:	fcc "ACIA1 FAIL "
	fcb 0

	rmb	$FFFE-*
	fdb	start			; the reset vector
