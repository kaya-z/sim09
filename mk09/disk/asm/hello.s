* hello.s - a program in assembler.   as /asm/hello.s -o /hello.mx   then   /hello.mx   (or: hello, as the shell looks in /bin only: cp /hello.mx /bin)
	runtime
	ldx	#msg		; the message
	lbsr	puts
	lda	#'!'
	lbsr	putc
	lda	#10
	lbsr	putc
	clrb			; the exit code
	rts
msg	fcc	"hello from an assembler program"
	fcb	0
