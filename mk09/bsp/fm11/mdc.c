/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mdc.c - FM-11 hard disk (MDC, $FDC0-$FDC3) with 256 byte sectors, CHR addressing: 32 sectors per track, 8 heads
 * (the drive type $0C, M2235B: 315 cylinders = 20MB).  Polled, no interrupt; the data goes by DMA channel 2.
 * The sequence is the one of the OS-9 driver (docs: emulator/Fm11gtk/docs/FM11_HDD_INTERFACE_SPEC.md).
 */
#include "bsp.h"

#define MDC_ST      (*(u8 *)0xFDC0)
#define MDC_CMD     (*(u8 *)0xFDC0)
#define MDC_DATA    (*(u8 *)0xFDC1)
#define MDC_CTL     (*(u8 *)0xFDC2)

#define DMA_CH      2
#define DMA_ADR_H   (*(u8 *)(0xFD40 + 4 * DMA_CH))
#define DMA_ADR_L   (*(u8 *)(0xFD41 + 4 * DMA_CH))
#define DMA_CNT_H   (*(u8 *)(0xFD42 + 4 * DMA_CH))
#define DMA_CNT_L   (*(u8 *)(0xFD43 + 4 * DMA_CH))
#define DMA_MODE    (*(u8 *)(0xFD50 + DMA_CH))
#define DMA_BANK    (*(u8 *)(0xFD94 + DMA_CH))
#define DMA_ENA     (*(u8 *)0xFD54)
#define DMA_IMSK    (*(u8 *)0xFD55)
#define DMA_CFG     (*(u8 *)0xFD56)

#define MDC_CTL_VAL (0x64)              /* the driver's $E4 without the interrupt enable: DMA channel 2 */

static u8 wait(u8 mask, u8 want)
{
    u16 t = 0;
    while ((MDC_ST & mask) != want)
        if (++t == 0)
            return 1;
    return 0;
}

/* one controller command: np parameter bytes are sent while the controller asks (PREQ), the 10 result bytes are read */
static u8 mdc_exec(u8 cmd, const u8 *pam, u8 *res)
{
    u8 n = 0;
    u16 t;

    if (wait(0x80, 0x80))
        return 1;
    MDC_CMD = cmd;
    while (MDC_ST & 0x20) {             /* PREQ */
        if (wait(0x01, 0x01))
            return 1;
        MDC_DATA = pam[n++];
        if (n > 8)
            break;
    }
    for (t = 0; !(MDC_ST & 0x40); ) {   /* RDMD: the result is ready */
        if (++t == 0 && wait(0x40, 0x40))
            return 1;
    }
    n = 0;
    while (MDC_ST & 0x40) {
        if (wait(0x01, 0x01))
            return 1;
        t = MDC_DATA;
        if (n < 10)
            res[n++] = (u8)t;
    }
    return (res[0] & 0xF0) == 0x10 ? 0 : 1;
}

void mdc_init(void)
{
    u8 pam[8], res[10];
    u16 i;

    MDC_CTL = 0x08;                     /* reset the controller */
    for (i = 0; i < 2000; i++)
        ;
    MDC_CTL = MDC_CTL_VAL;
    DMA_ENA |= 1 << DMA_CH;
    DMA_IMSK &= ~(1 << DMA_CH);
    DMA_CFG = 0x08;
    for (i = 0; i < 8; i++)
        pam[i] = 0;
    mdc_exec(0x10, pam, res);           /* recalibrate */
}

u8 mdc_sector_io(u8 drive, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write)
{
    u8 pam[8], res[10], tries;
    u16 a = (u16)buf;

    MDC_CTL = (MDC_CTL_VAL & 0xFC) | (drive >> 1);
    pam[0] = drive & 1;
    pam[1] = 0;
    pam[2] = hi;                        /* cylinder = lsn / 256 */
    pam[3] = mid;
    pam[4] = (lo >> 5) & 7;             /* head */
    pam[5] = lo & 31;                   /* record */
    pam[6] = 1;                         /* one sector: $0100 bytes */
    pam[7] = 0;
    for (tries = 0; tries < 3; tries++) {
        DMA_ADR_H = (u8)(a >> 8);
        DMA_ADR_L = (u8)a;
        DMA_CNT_H = 1;
        DMA_CNT_L = 0;
        DMA_BANK = 0;
        DMA_MODE = write ? 0x05 : 0x04;     /* T/H = 1: burst; bit 0 = memory to device */
        if (!mdc_exec(write ? 0x23 : 0x22, pam, res))
            return 0;
    }
    return 1;
}
