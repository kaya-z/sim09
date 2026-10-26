* cat.s - copies the standard input to the standard output (a program with a buffer in the bss)
*   as /asm/cat.s -o /bin/cat2.mx      then      echo hello | cat2
	runtime
	stack	300
loop	ldx	#buf
	ldy	#128
	clrb			; fd 0, the standard input
	lbsr	read
	cmpd	#0
	ble	done		; the end of the input, or an error
	tfr	d,y
	ldx	#buf
	ldb	#1		; fd 1, the standard output
	lbsr	write
	bra	loop
done	clrb
	rts
	bss
buf	rmb	128
