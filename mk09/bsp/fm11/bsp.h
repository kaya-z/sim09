/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * bsp.h - board support: the FUJITSU FM-11 (main CPU MB68B09, 6809 mode, MMU off = flat 64KB)
 *
 *   $0000-$FBFF   RAM (the boot ROM overlay at $5000-$5FFF is switched off by $FD93 bit 0)
 *   $FC00-$FC7F   RAM            $FC80-$FCFF  shared memory with the sub CPU (the display sub system)
 *   $FD00-$FDFF   I/O            $FE00-$FFFF  RAM; $FFF2-$FFFD are the interrupt vectors (RAM)
 *
 *   $FD00/$FD01   key code (16 bit; $FD01 = ASCII), read of $FD01 takes it;  $FD02 interrupt mask (bit 7 key, bit 4 PTM)
 *   $FD03         interrupt flags (bit 6 key, bit 0 PTM, bit 5 extension IRQ)
 *   $FD05         sub CPU: write bit 7 = HALT, bit 6 = cancel IRQ; read bit 7 = busy / halted
 *   $FC80...      the sub system command: [1] continuation flag, [2] command code, [3...] parameters
 *   $FD38-$FD3F   MC6840 PTM (timer 2 = the tick)
 *   $FD93         bit 7 MMU on, bit 0 boot ROM overlay off
 *   $FDC0-$FDC3   hard disk controller (MDC), data through the DMA controller ($FD40-$FD57, $FD94-$FD97)
 *   $FD30-$FD37   floppy controller of the 8 inch / 2HD drives (MB8877), $FD18-$FD1F the 5 inch (mini) one
 *
 * The console is the display sub system: the console server collects its output and sends it with
 * the PUT STRING command (bsp_con_put / bsp_con_flush).  The disk is the MDC hard disk or the 2HD floppy,
 * 256 byte sectors: bsp_sector_io().
 */
#ifndef BSP_H
#define BSP_H

#include "types.h"

#define BOARD_FM11  1

#define DISK_READ_CMD   0x81
#define DISK_WRITE_CMD  0x55

#define FM_SUBSTAT  (*(u8 *)0xFD05)
#define FM_IO(a)    (*(u8 *)(a))

/* console output (console server) */
void bsp_con_put(u8 c);
void bsp_con_flush(void);
void bsp_con_init(void);       /* graphic display and palette on (the kanji are drawn into the graphic planes), kanji mode of the console */

/* the disk (disk server): one 256 byte sector of the drive; lsn = hi:mid:lo (24 bit), buf below $FC00; 0 = good.
 * BOOTDEV=hd: drive 0 = the hard disk (MDC), drives 1-4 = the 2HD floppy units 0-3 (initialised when first used)
 * BOOTDEV=2hd: the drives 0-3 = the 2HD floppy units 0-3 */
void bsp_disk_init(void);
u8   bsp_sector_io(u8 drive, u8 hi, u8 mid, u8 lo, u8 *buf, u8 write);
/* low level format of one track side of a floppy (MFM, 26 sectors of 256 bytes): buf = the track image, len bytes (the controller reads it by DMA).
 * Not for the system drive 0.  0 = good */
u8   bsp_track_format(u8 drive, u8 track, u8 side, u8 *buf, u16 len);

/* for the servers */
void bsp_now_fat(u16 *date, u16 *time);   /* the clock in the FAT format */
void bsp_clock_init(void);               /* the clock of the system (kept by the kernel) starts at 2026-01-01 */
void bsp_set_clock(u16 ym, u16 dh, u16 ms);   /* set the clock: year (since 1980):month, day:hour, minute:second */

void bsp_timer_start(void);
u8   bsp_timer_fired(void);
void bsp_power_off(void);

#endif
