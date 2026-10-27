* start.s - the start of a C program: X = the start-up block of PM (see lib/mk_start.c)
	pshs	x
	lbsr	_mk_start
	bra	*
