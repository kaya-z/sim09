/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* rtc.c - sbc09 board: the real time clock */
#include "bsp.h"

void bsp_rtc_read(u8 *tm)
{
    u8 i;
    TIMER_REG = 0x04;               /* latch the clock */
    for (i = 0; i < 6; i++)
        tm[i] = RTC_REG(1 + i);
}

#ifdef MMU_COCO
/* set the clock: the registers $31-$36 (year-1900, month, day, hour, minute, second), then the command $05 (a = year since 1980: the FAT year + 80) */
void bsp_set_clock(u16 ym, u16 dh, u16 ms)
{
    RTC_REG(1) = (u8)((ym >> 8) + 80);
    RTC_REG(2) = (u8)ym;
    RTC_REG(3) = (u8)(dh >> 8);
    RTC_REG(4) = (u8)dh;
    RTC_REG(5) = (u8)(ms >> 8);
    RTC_REG(6) = (u8)ms;
    TIMER_REG = 0x05;
}
#endif

/* the time in FAT format: date = (year-1980) << 9 | month << 5 | day, time = hour << 11 | minute << 5 | second / 2 */
void bsp_now_fat(u16 *date, u16 *time)
{
    u8 tm[6];
    bsp_rtc_read(tm);
    if (tm[0] < 80) {                       /* no clock: 1980-01-01 */
        *date = (1 << 5) | 1;
        *time = 0;
        return;
    }
    *date = ((u16)(tm[0] - 80) << 9) | ((u16)tm[1] << 5) | tm[2];
    *time = ((u16)tm[3] << 11) | ((u16)tm[4] << 5) | (tm[5] >> 1);
}
