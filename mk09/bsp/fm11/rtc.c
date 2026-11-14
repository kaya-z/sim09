/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * rtc.c - FM-11: the clock.  The FM-11 has no calendar clock that the main CPU can read (the ROM of the display sub system has no clock command, and
 * there is no battery), so the clock is the system's: the kernel counts the seconds in the ticks of the timer and keeps the calendar (year since 1980,
 * month, day, hour, minute, second), see SYS_TIME in sysnum.h (a privileged call: the FS server uses it).  At the boot the clock starts at
 * 2026-01-01 00:00:00; "date -s" sets it.
 */
#include "bsp.h"
#include "mk09.h"

void bsp_now_fat(u16 *date, u16 *time)
{
    u16 ym = mk_time(0, 0), dh = mk_time(1, 0), ms = mk_time(2, 0);     /* year:month, day:hour, minute:second (a snapshot of the kernel) */
    *date = ((ym >> 8) << 9) | ((ym & 0xFF) << 5) | (dh >> 8);
    *time = ((dh & 0xFF) << 11) | ((ms >> 8) << 5) | ((ms & 0xFF) >> 1);
}

void bsp_set_clock(u16 ym, u16 dh, u16 ms)     /* year (since 1980):month, day:hour, minute:second (the caller has checked them) */
{
    mk_time(3, ym);
    mk_time(4, dh);
    mk_time(5, ms);
    mk_time(6, 0);
}

void bsp_clock_init(void)
{
    bsp_set_clock((46 << 8) | 1, (1 << 8), 0);                          /* 2026-01-01 00:00:00 */
}
