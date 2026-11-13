/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * fs_core.h - the FAT16 file system code of the FS server
 *
 * fs_core.c does not know about messages: it needs four services from its environment
 * (the glue fs.c on MK09, or the test program tests/fs_host.c on the host).
 *
 * Limits (they keep the code small on an 8 bit CPU): FAT16 with clusters of up to 64 blocks of
 * 512 bytes (32KB), so a volume and a file have up to 2GB; the volume starts at a block below
 * 2^23 (the disk server takes block numbers of 23 bits: 4GB).  Sizes and positions are 32 bit values
 * passed as (hi, lo) 16 bit pairs.
 */
#ifndef FS_CORE_H
#define FS_CORE_H

#include "types.h"
#include "proto.h"

/* read (write = 0) or write one 512 byte block (the number bhi:blo is relative to the start of the
 * volume: add fs_part_hi:fs_part_lo); returns E_OK or E_IO */
u8   fsx_disk(u8 write, u16 bhi, u16 blo, u8 *buf);
/* data of the client's granted buffer (offset from its start) */
u8   fsx_get(u8 client, u16 off, void *dst, u16 n);
u8   fsx_put(u8 client, u16 off, const void *src, u16 n);
/* the time in FAT format */
void fsx_now(u16 *date, u16 *time);

extern u16 fs_part_hi, fs_part_lo;     /* first block of the partition (set by fs_mount) */

#define NHANDLE     6

u8   fs_mount(void);
u8   fs_open(const char *path, u8 flags, u8 owner, u8 *h);
u8   fs_close(u8 h);
u8   fs_read(u8 h, u8 client, u16 n, u16 *got);
u8   fs_write(u8 h, u8 client, u16 n, u16 *got);
/* whence 0: from the start, 1: from here, 2: from the end; (hi, lo) is signed */
u8   fs_seek(u8 h, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo);
u8   fs_fstat(u8 h, u8 *attr, u16 *shi, u16 *slo);
u8   fs_stat(const char *path, u8 *attr, u16 *shi, u16 *slo, u16 *date);
u8   fs_readdir(u8 h, dirent_t *d);
u8   fs_mkdir(const char *path);
u8   fs_remove(const char *path);
u8   fs_rename(const char *from, const char *to);
u8   fs_chdir(const char *path);
u8   fs_getcwd(char *buf, u16 n);
u8   fs_sync(void);
void fs_closeall(u8 owner);
u8   fs_chown(u8 h, u8 owner);
u8   fs_statfs(u8 drive, u8 *spc, u16 *clusters, u16 *freec);
u8   fs_mount_drive(u8 d);         /* mount the volume of the drive d (0-4) */
u8   fs_umount(u8 d);              /* drives 1-4; E_BUSY while a file of it is open */
u8   fs_mounted(u8 d);
u8   fs_select(u8 d);              /* make the volume of the drive d the current one (0: the system drive) */
extern u8 fs_drive;                /* the drive of the volume that the cache holds */

#endif
