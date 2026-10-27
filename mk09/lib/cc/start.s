* start.s - the start of a C program (first in the file that `as` reads): X = the start-up block of PM (see mk_start in clib.c)
;@use _mk_start
	pshs	x
	lbsr	_mk_start
	bra	*
