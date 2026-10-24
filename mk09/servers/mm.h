/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mm.h - the memory manager (servers/mm.s, a task of its own: TID_MM; protocol MM_ALLOC / MM_FREE / MM_INFO in proto.h)
 *
 * It keeps the books of the transient program area (TPA_BASE .. TPA_END, memmap.h) in pages of 256 bytes: which pages are free.
 * PM is its client: it asks for n pages in a row (from the bottom of the area for a program, from the top for the shell) and
 * gives them back when the program has exited.  MM does not protect anything: the kernel keeps the region [lo, hi) of every
 * task and checks it.  A page is a number 0 .. MM_NPAGES - 1; MM_ADDR() turns it into an address.
 */
#ifndef MM_H
#define MM_H

#include "types.h"
#include "memmap.h"

#define MM_PAGE     256
#define MM_NPAGES   ((TPA_END - TPA_BASE) / MM_PAGE)
#define MM_ADDR(i)  (TPA_BASE + ((u16)(i) << 8))

#endif
