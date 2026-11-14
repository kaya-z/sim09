/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * fdc.c - FM-11 floppy disk: the 2HD drives on the MB8877 controller ($FD30-$FD37), data by DMA channel 1, polled.
 *
 * The raw 2HD image of the FM-11 (1021696 bytes, FDD_2HD_256) has 77 tracks of 2 sides of 26 sectors; the sectors of track 0 side 0 are
 * 128 bytes (FM), all the others 256 bytes.  The 256 byte sectors of MK09 start behind that track:
 *     lsn 0 = track 0 side 1 sector 1, then the sectors 1..26, the other side, the next track ...  (3978 sectors, 994 KB)
 * Track 0 side 0 holds the IPL (the boot ROM reads its sectors 1-4, 4 x 128 bytes, to $0400): boot/ipl_fm11_2hd.s.
 * (A disk that the format command made has 256 byte sectors on that track too: it is a data disk, not a boot disk.)
 * The sequence is the one of the boot ROM ($5516...): select, seek ($1A), side, sector, DMA, read ($80) / write ($A0) / write track ($F0),
 * wait for the interrupt flag $FD37 bit 6, status.
 */
#include "bsp.h"

#define FDC_CMD     (*(u8 *)0xFD30)
#define FDC_TRK     (*(u8 *)0xFD31)
#define FDC_SEC     (*(u8 *)0xFD32)
#define FDC_DAT     (*(u8 *)0xFD33)
#define FDC_SIDE    (*(u8 *)0xFD34)
#define FDC_DRV     (*(u8 *)0xFD35)
#define FDC_IRQ     (*(u8 *)0xFD37)

#define DMA_CH      1
#define DMA_ADR_H   (*(u8 *)(0xFD40 + 4 * DMA_CH))
#define DMA_ADR_L   (*(u8 *)(0xFD41 + 4 * DMA_CH))
#define DMA_CNT_H   (*(u8 *)(0xFD42 + 4 * DMA_CH))
#define DMA_CNT_L   (*(u8 *)(0xFD43 + 4 * DMA_CH))
#define DMA_MODE    (*(u8 *)(0xFD50 + DMA_CH))
#define DMA_BANK    (*(u8 *)(0xFD94 + DMA_CH))
#define DMA_ENA     (*(u8 *)0xFD54)
#define DMA_IMSK    (*(u8 *)0xFD55)
#define DMA_CFG     (*(u8 *)0xFD56)
#define DMA_CLR     (*(u8 *)0xFD57)

static u8 dmabank;                      /* the physical address bits 19-16 of the DMA (0: the memory is below 64KB) */
static u8 curtrk[4];                    /* the track of each unit's head (the controller has one track register) */
static u8 curunit = 0xFF;

/* the command is done when the interrupt flag is set (the controller has no interrupt here: $FD2C is 0) */
static u8 done(void)
{
    u8 o, i;
    for (o = 0; o < 6; o++)
        for (i = 0; ; i++) {
            u8 t;
            u16 n;
            for (n = 0; n < 0x4000; n++) {
                t = FDC_IRQ;
                if (t & 0x40)
                    return 0;
            }
            if (i == 3)
                break;
        }
    return 1;
}

static u8 seek(u8 unit, u8 track)
{
    u8 r;

    if (track == 0) {
        FDC_CMD = 0x0A;                 /* restore */
    } else {
        FDC_DAT = track;
        FDC_CMD = 0x1A;                 /* seek */
    }
    r = done();
    if (!r)
        curtrk[unit] = track;
    return r;
}

static void select(u8 unit)
{
    if (unit != curunit) {
        FDC_DRV = (FDC_DRV & 0x80) | unit;   /* the boot ROM writes the unit number only (bit 7 as it was) */
        FDC_TRK = curtrk[unit];         /* the track register of that unit */
        curunit = unit;
    }
}

void fdc_init(void)
{
    DMA_ENA |= 1 << DMA_CH;
    DMA_IMSK &= ~(1 << DMA_CH);
    DMA_CFG = 0x08;
    FDC_DRV = FDC_DRV & 0x80;
    curunit = 0;
    seek(0, 0);
}

static void dma(u16 a, u16 n, u8 mode)
{
    DMA_CLR = 0;
    DMA_ADR_H = (u8)(a >> 8);
    DMA_ADR_L = (u8)a;
    DMA_CNT_H = (u8)(n >> 8);
    DMA_CNT_L = (u8)n;
    DMA_BANK = dmabank;
    DMA_MODE = mode;
}

u8 fdc_sector_io(u8 unit, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write)
{
    u16 lsn = ((u16)mid << 8) | lo;
    u16 ts;
    u8 track, side, sec, tries, st;

    if (hi || lsn >= 3978)
        return 1;
    ts = lsn / 26 + 1;                  /* track side: 0 is the 128 byte track */
    sec = (u8)(lsn % 26) + 1;
    track = (u8)(ts >> 1);
    side = (u8)(ts & 1);
    select(unit);
    for (tries = 0; tries < 4; tries++) {
        if (tries == 2)
            seek(unit, 0);              /* recalibrate, then seek again */
        if (curtrk[unit] != track && seek(unit, track))
            continue;
        FDC_SIDE = side | 0x40;         /* bit 6 = density (variant A: 1 for the MFM tracks), bit 4 = 0 */
        FDC_SEC = sec;
        dma((u16)buf, 256, write ? 0x05 : 0x04);
        FDC_CMD = write ? 0xA0 : 0x80;
        if (done())
            continue;
        st = FDC_CMD;                   /* status: not ready, (write protect), record type / write fault, not found, CRC, lost data */
        if (!(st & (write ? 0xFC : 0x9C)))
            return 0;
    }
    return 1;
}

/* write one track side: the track image (gaps, address marks, data) goes to the controller by DMA */
u8 fdc_format_track(u8 unit, u8 track, u8 side, u16 addr, u8 bank, u16 len)
{
    u8 tries, st;

    if (track > 76 || side > 1)
        return 1;
    select(unit);
    for (tries = 0; tries < 2; tries++) {
        if (tries == 1)
            seek(unit, 0);
        if (curtrk[unit] != track && seek(unit, track))
            continue;
        FDC_SIDE = side | 0x40;         /* bit 6 = density (variant A: 1 for the MFM tracks), bit 4 = 0 */
        dmabank = bank;
        dma(addr, len, 0x05);
        dmabank = 0;
        FDC_CMD = 0xF0;                 /* write track */
        if (done())
            continue;
        st = FDC_CMD;
        if (!(st & 0xFC))
            return 0;
    }
    return 1;
}
