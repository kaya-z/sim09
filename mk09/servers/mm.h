/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mm.h - the memory manager (part of PM, servers/mm.c)
 *
 * It hands out the transient program area (TPA_BASE .. TPA_END, memmap.h) in pages of 256 bytes to
 * the programs and the shell.  It only keeps the books - which pages are free; the kernel is the one
 * that protects a program's region (a task has a region [lo, hi) and may only touch that).
 * Everything is a page number (0 .. MM_NPAGES - 1); MM_ADDR() turns it into an address.
 */
#ifndef MM_H
#define MM_H

#include "types.h"
#include "memmap.h"

#define MM_PAGE     256
#define MM_NPAGES   ((TPA_END - TPA_BASE) / MM_PAGE)
#define MM_ADDR(i)  (TPA_BASE + ((u16)(i) << 8))

void mm_init(void);
/* n pages in one piece: the first fit from the bottom (top == 0) or from the top of the area; returns the first page, or -1 */
int  mm_alloc(u8 n, u8 top);
void mm_free(u8 first, u8 n);
/* the total number of pages, the free pages and the biggest free piece */
void mm_info(u8 *total, u8 *freep, u8 *largest);

#endif
