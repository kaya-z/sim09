/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* fmtimg.c - the image of one track side for the write track command of the floppy controller: IBM MFM, 26 sectors of 256 bytes, interleave 1
 * (Level 2: the disk server builds it in its track buffer; Level 1: apps/format.c builds it in its own memory) */
#include "bsp.h"

#define NSEC        26
#define TRACKLEN    10416

static u8 *fill(u8 *p, u8 v, u16 n)
{
    while (n--) *p++ = v;
    return p;
}

u16 fmt_build(u8 *trk, u8 track, u8 side)
{
    u8 *p = trk;
    u8 s;

    p = fill(p, 0x4E, 80);
    p = fill(p, 0x00, 12);
    p = fill(p, 0xF6, 3);
    *p++ = 0xFC;                        /* index mark */
    p = fill(p, 0x4E, 50);
    for (s = 0; s < NSEC; s++) {
        p = fill(p, 0x00, 12);
        p = fill(p, 0xF5, 3);
        *p++ = 0xFE;                    /* ID address mark */
        *p++ = track;                   /* C */
        *p++ = side;                    /* H */
        *p++ = s + 1;                   /* R */
        *p++ = 1;                       /* N: 256 bytes */
        *p++ = 0xF7;                    /* CRC */
        p = fill(p, 0x4E, 22);
        p = fill(p, 0x00, 12);
        p = fill(p, 0xF5, 3);
        *p++ = 0xFB;                    /* data mark */
        p = fill(p, 0xE5, 256);
        *p++ = 0xF7;                    /* CRC */
        p = fill(p, 0x4E, 54);
    }
    fill(p, 0x4E, (u16)(TRACKLEN - (u16)(p - trk)));
    return TRACKLEN;
}
