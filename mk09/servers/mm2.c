/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mm2.c - the memory manager of the builds with an MMU (the FM-11 Level 2; the sbc09 with the MMU; TASK 5, protocol MM_* in proto.h)
 *
 * It keeps the books of the physical pages of 4KB that the system does not use (the first 128KB of the machine: MM_POOL0 / MM_POOL1 of memmap.h; every further
 * step of 128KB that the kernel found at the start, up to 1MB):
 * PM asks for n pages and gets a list of page numbers (written to its buffer, the grant of the call), and gives them back with the list.
 * The kernel puts them into the map of the new program (tcreate).  MM_INFO tells the numbers in pages of 256 bytes, as in Level 1.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "memmap.h"

static u16 freem[16];                   /* free pages: bit n of freem[k] = the physical page 16 * k + n (up to 1MB = 256 pages) */
static u8 total;
static u16 npages;                      /* its pages (the product by a shift: 32 * 8 does not go into a byte) */
#ifdef MMU_COCO
/* the sbc09 with the MMU: the pages are the blocks of 8KB; the physical blocks 8 .. $3E (432KB) are for the programs (the blocks 0-7 are the system, $3F has the
 * fixed page); the RAM above is not used yet (no memprobe) */
#define POOL_LO 8
#define POOL_HI 0x3E
#else
static u8 ngrp;                         /* the RAM of the machine in steps of 128KB (32 pages): the kernel has tried it at the start (memprobe) */
#define MEMGRP  (*(volatile u8 *)(K_DATA + K_DSZ - 0x91))
#endif

#ifdef MMU_COCO
#define NFREEM  16
#else
#define NFREEM  (2 * ngrp)
#endif
static u8 count(void)
{
    u8 n = 0, i;
    u16 w;
    for (i = 0; i < NFREEM; i++)
        for (w = freem[i]; w; w >>= 1)
            if (w & 1) n++;
    return n;
}

int main(void)
{
    msg_t m, r;
    u8 list[16], i, n, got;
    u16 p;

#ifdef MMU_COCO
    npages = 256;
    for (p = POOL_LO; p <= POOL_HI; p++) freem[p >> 4] |= 1u << (p & 15);
#else
    ngrp = MEMGRP;
    if (ngrp < 1 || ngrp > 8) ngrp = 1;
    npages = ngrp;
    npages <<= 5;
    freem[0] = MM_POOL0;
    freem[1] = MM_POOL1;
    for (i = 2; i < 2 * ngrp; i++) freem[i] = 0xFFFF;      /* (the steps above the first 128KB are all free) */
#endif
    total = count();
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) continue;
        r.op = E_NOSYS;
        r.a = r.b = r.c = r.d = 0;
        switch (m.op) {
        case MM_ALLOC:
            if (m.src != TID_PM) { r.op = E_PERM; break; }
            n = (u8)m.a;
            if (n == 0 || n > NUP || m.c < n) { r.op = E_INVAL; break; }
            if (count() < n) { r.op = E_NOMEM; break; }
            got = 0;
            for (p = 0; p < npages && got < n; p++)
                if (freem[p >> 4] & (1u << (p & 15))) {
                    freem[p >> 4] &= ~(1u << (p & 15));
                    list[got++] = p;
                }
            r.op = mk_copyto(m.src, 0, list, n);
            if (r.op) {                                 /* (the buffer was not good: take the pages back) */
                for (i = 0; i < got; i++) freem[list[i] >> 4] |= 1u << (list[i] & 15);
            }
            break;
        case MM_FREE:
            if (m.src != TID_PM) { r.op = E_PERM; break; }
            n = (u8)m.a;
            if (n == 0 || n > NUP || m.c < n) { r.op = E_INVAL; break; }
            r.op = mk_copyfrom(m.src, 0, list, n);
            if (r.op) break;
            for (i = 0; i < n; i++)
                if (list[i] < npages) freem[list[i] >> 4] |= 1u << (list[i] & 15);
            break;
#ifndef MMU_COCO
        case MM_MAP:
            r.op = E_OK;
            i = (u8)m.a;                                /* the word to start with (0, 2, ..): the free pages 16 * i .. 16 * i + 31 */
            if (i > 2 * ngrp - 2) i = 0;
            r.a = freem[i];
            r.b = freem[i + 1];
            r.c = ngrp;
            break;
#endif
        case MM_INFO:
            r.op = E_OK;
            r.a = (u16)total << PGSH;
            r.b = (u16)count() << PGSH;
            r.c = r.b > ((u16)NUP << PGSH) ? ((u16)NUP << PGSH) : r.b;
            break;
        }
        mk_reply(m.src, &r);
    }
    return 0;
}
