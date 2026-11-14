/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mm2.c - the memory manager of Level 2 (the MMU of the FM-11; TASK 5, protocol MM_* in proto.h)
 *
 * It keeps the books of the physical pages of 4KB that the system does not use (the first 128KB of the machine, MM_POOL0 / MM_POOL1 of memmap.h):
 * PM asks for n pages and gets a list of page numbers (written to its buffer, the grant of the call), and gives them back with the list.
 * The kernel puts them into the map of the new program (tcreate).  MM_INFO tells the numbers in pages of 256 bytes, as in Level 1.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "memmap.h"

static u16 freem[2];                    /* free pages: bit n of freem[0] = the physical page n, of freem[1] = n + 16 */
static u8 total;

static u8 count(void)
{
    u8 n = 0, i;
    u16 w;
    for (i = 0; i < 2; i++)
        for (w = freem[i]; w; w >>= 1)
            if (w & 1) n++;
    return n;
}

int main(void)
{
    msg_t m, r;
    u8 list[16], i, n, p, got;

    freem[0] = MM_POOL0;
    freem[1] = MM_POOL1;
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
            if (n == 0 || n > 13 || m.c < n) { r.op = E_INVAL; break; }
            if (count() < n) { r.op = E_NOMEM; break; }
            got = 0;
            for (p = 0; p < 32 && got < n; p++)
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
            if (n == 0 || n > 13 || m.c < n) { r.op = E_INVAL; break; }
            r.op = mk_copyfrom(m.src, 0, list, n);
            if (r.op) break;
            for (i = 0; i < n; i++)
                if (list[i] < 32) freem[list[i] >> 4] |= 1u << (list[i] & 15);
            break;
        case MM_MAP:
            r.op = E_OK;
            r.a = freem[0];
            r.b = freem[1];
            break;
        case MM_INFO:
            r.op = E_OK;
            r.a = (u16)total << 4;
            r.b = (u16)count() << 4;
            r.c = r.b > 13 * 16 ? 13 * 16 : r.b;
            break;
        }
        mk_reply(m.src, &r);
    }
    return 0;
}
