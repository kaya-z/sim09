/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* bsp_disk.c - FM-11: which drive is which (see bsp.h): the hard disk (mdc.c) and the 2HD floppy units (fdc.c) */
#include "bsp.h"

void mdc_init(void);
u8   mdc_sector_io(u8 drive, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write);
void fdc_init(void);
u8   fdc_sector_io(u8 unit, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write);
u8   fdc_format_track(u8 unit, u8 track, u8 side, u8 *buf, u16 len);

#ifdef FM11_2HD
#define FIRSTFD 0
#else
#define FIRSTFD 1
#endif

static u8 fdc_ready;

void bsp_disk_init(void)
{
#ifdef FM11_2HD
    fdc_init();
    fdc_ready = 1;
#else
    mdc_init();
#endif
}

static u8 fd_up(void)
{
    if (!fdc_ready) {
        fdc_init();
        fdc_ready = 1;
    }
    return 0;
}

u8 bsp_sector_io(u8 drive, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write)
{
#ifndef FM11_2HD
    if (drive == 0)
        return mdc_sector_io(0, hi, mid, lo, buf, write);
#endif
    if (drive < FIRSTFD || drive >= FIRSTFD + 4)
        return 1;
    fd_up();
    return fdc_sector_io(drive - FIRSTFD, hi, mid, lo, buf, write);
}

u8 bsp_track_format(u8 drive, u8 track, u8 side, u8 *buf, u16 len)
{
    if (drive == 0 || drive >= FIRSTFD + 4)       /* never the system drive */
        return 1;
    fd_up();
    return fdc_format_track(drive - FIRSTFD, track, side, buf, len);
}
