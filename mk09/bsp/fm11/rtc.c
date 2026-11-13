/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* rtc.c - FM-11: the main board has no calendar clock that the main CPU reads directly: a fixed date */
#include "bsp.h"

void bsp_rtc_read(u8 *tm)
{
    tm[0] = 126;        /* 2026 */
    tm[1] = 1;
    tm[2] = 1;
    tm[3] = 0;
    tm[4] = 0;
    tm[5] = 0;
}

void bsp_now_fat(u16 *date, u16 *time)
{
    *date = (u16)((2026 - 1980) << 9) | (1 << 5) | 1;
    *time = 0;
}
