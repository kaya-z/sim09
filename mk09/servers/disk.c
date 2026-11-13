/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * disk.c - the disk server (TASK 2)
 *
 * Block device for the SD card (or a disk image of the simulator).  The client sees 512 byte
 * blocks; the hardware moves 256 byte sectors with DMA (bsp.h), so a block is two sectors.
 * The data goes through a 256 byte buffer of this server and is copied to / from the client's granted
 * buffer by the kernel (no direct DMA into a client: the same code works with an MMU).
 * A command goes to the hardware in its asynchronous mode (DISK_CTL): the command returns at once and the server waits for the notification
 * NFY_DISK of the interrupt at the end of the command (the kernel), so the other tasks use the time (a write waits for the card for milliseconds).
 * A board without the asynchronous mode has finished the command when it returns, and the result is read at once.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "bsp.h"

static u8 buf[256];

static u8 sector_io(u8 drive, u16 lba_lo, u8 lba_hi, u8 half, u8 *p, u8 cmd)
{
    msg_t m;
    u8 st;
    /* sector number = block * 2 + half (24 bits in the interface) */
    u8 lo = (u8)(lba_lo << 1) | half;
    u8 hi = (u8)(lba_hi << 1) | (u8)(lba_lo >> 15);

#ifdef BOARD_FM11
    (void)m;
    (void)st;
    return bsp_sector_io(drive, hi, (u8)((lba_lo >> 7) & 0xFF), lo, p, cmd == DISK_WRITE_CMD) ? E_IO : E_OK;
#else
    DISK_DRIVE = drive;
    DISK_LSN_HI = hi;
    DISK_LSN_MID = (u8)((lba_lo >> 7) & 0xFF);
    DISK_LSN_LO = lo;
    DISK_DMA = (u16)p;
    DISK_CTL = 0x81;                    /* asynchronous, with the interrupt at the end */
    DISK_CMD = cmd;
    while ((st = DISK_CMD) == DISK_BUSY)
        mk_recv(TID_ANY, &m);           /* (only the notification of the kernel comes: the only client waits for our reply) */
    return st == 0 ? E_OK : E_IO;
#endif
}

/* one half (sector) of a block between the card and the client's buffer */
static u8 half_io(u8 client, u8 drive, u16 lba_lo, u8 lba_hi, u8 half, u8 write)
{
    u8 st;
    if (write) {
        st = mk_copyfrom(client, half ? 256 : 0, buf, 256);
        if (st == E_OK) st = sector_io(drive, lba_lo, lba_hi, half, buf, DISK_WRITE_CMD);
    } else {
        st = sector_io(drive, lba_lo, lba_hi, half, buf, DISK_READ_CMD);
        if (st == E_OK) st = mk_copyto(client, half ? 256 : 0, buf, 256);
    }
    return st;
}

int main(void)
{
    msg_t m, r;
    u8 st;

#ifdef BOARD_FM11
    bsp_disk_init();
#endif
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) continue;
        r.op = E_NOSYS;
        r.a = r.b = r.c = r.d = 0;
        switch (m.op) {
        case DISK_READ:
            if (m.c != BLKSIZE || (m.d & 0xFF) > 0x7F) { r.op = E_INVAL; break; }
            st = half_io(m.src, (u8)(m.d >> 8), m.a, (u8)m.d, 0, 0);
            if (st == E_OK) st = half_io(m.src, (u8)(m.d >> 8), m.a, (u8)m.d, 1, 0);
            r.op = st;
            break;
        case DISK_WRITE:
            if (m.c != BLKSIZE || (m.d & 0xFF) > 0x7F) { r.op = E_INVAL; break; }
            st = half_io(m.src, (u8)(m.d >> 8), m.a, (u8)m.d, 0, 1);
            if (st == E_OK) st = half_io(m.src, (u8)(m.d >> 8), m.a, (u8)m.d, 1, 1);
            r.op = st;
            break;
#ifdef BOARD_FM11
        case DISK_FORMAT:
            if (m.b < 0x0600 || m.c == 0 || (u16)(m.b + m.c) > 0xFB00 || (m.a >> 1) > 76) { r.op = E_INVAL; break; }
            r.op = bsp_track_format((u8)(m.d >> 8), (u8)(m.a >> 1), (u8)(m.a & 1), (u8 *)m.b, m.c) ? E_IO : E_OK;
            break;
#endif
        case DISK_SIZE:
            r.op = E_OK;
            r.a = 1;
            break;
        }
        mk_reply(m.src, &r);
    }
    return 0;
}
