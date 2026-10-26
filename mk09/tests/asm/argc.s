* argc.s - prints the number of arguments (the name of the program counts)
	runtime
	ldx	__sb		; the start-up block: [3] = argc
	lda	3,x
	adda	#'0'
	lbsr	putc
	lda	#10
	lbsr	putc
	clrb
	rts
