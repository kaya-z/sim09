/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * format - format a 2HD floppy disk of the FM-11 (BOARD=fm11)
 *
 *     format [-y] drive [LABEL]
 *
 * drive 1-4 (the drive 0 is the system disk: the hard disk, or the floppy that the system runs from, and is never formatted).
 * First every track side (77 x 2) is formatted at the low level: MFM, 26 sectors of 256 bytes (DISK_FORMAT: the disk server writes the
 * track image of this program with the floppy controller).  Then the MK09 structures are written with DISK_WRITE: the MBR with a FAT16
 * partition (block 1 on), the boot sector, the two FATs and the empty root directory (clusters of 2 blocks, 1 KB: 973 clusters).
 * At the end every block is read back (a surface check), and the boot sector and the first FAT block are checked.  The MK09 disk of a floppy has 3978 sectors = 1989 blocks (the
 * layout of bsp/fm11/fdc.c: lsn 0 = track 0 side 1 sector 1); track 0 side 0 is formatted too, but not used (a data disk, not a boot disk).
 */
#include "types.h"
#include "mklib.h"
#define TOOL_VERSION "1.0"

#define NSEC        26
#define TRACKLEN    10416
#define TOTBLK      1989                /* blocks of the disk */
#define PARTBLK     1                   /* the volume starts at block 1 (block 0: the MBR) */
#define VOLBLK      (TOTBLK - PARTBLK)
#define SPC         2
#define NFATS       2
#define ROOTENT     512
#define ROOTBLK     (ROOTENT * 32 / 512)

static u8 trk[TRACKLEN];
static u8 blk[BLKSIZE];

static u16 fill(u8 *p, u8 v, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++) *p++ = v;
    return n;
}

/* the image of one track: the standard IBM MFM format, 26 sectors of 256 bytes, interleave 1.  The positions of the C and H bytes
 * of the 26 address marks are kept for the next tracks */
static u16 idpos[NSEC];

static void build_track(void)
{
    u8 *p = trk;
    u8 s;

    p += fill(p, 0x4E, 80);
    p += fill(p, 0x00, 12);
    p += fill(p, 0xF6, 3);
    *p++ = 0xFC;                        /* index mark */
    p += fill(p, 0x4E, 50);
    for (s = 0; s < NSEC; s++) {
        p += fill(p, 0x00, 12);
        p += fill(p, 0xF5, 3);
        *p++ = 0xFE;                    /* ID address mark */
        idpos[s] = (u16)(p - trk);
        *p++ = 0;                       /* C */
        *p++ = 0;                       /* H */
        *p++ = s + 1;                   /* R */
        *p++ = 1;                       /* N: 256 bytes */
        *p++ = 0xF7;                    /* CRC */
        p += fill(p, 0x4E, 22);
        p += fill(p, 0x00, 12);
        p += fill(p, 0xF5, 3);
        *p++ = 0xFB;                    /* data mark */
        p += fill(p, 0xE5, 256);
        *p++ = 0xF7;                    /* CRC */
        p += fill(p, 0x4E, 54);
    }
    fill(p, 0x4E, (u16)(TRACKLEN - (u16)(p - trk)));
}

static int call(u8 op, u8 drive, u16 a, u8 *buf, u16 len, u8 gfl)
{
    msg_t m;
    m.op = op;
    m.a = a;
    m.b = (u16)buf;
    m.c = len;
    m.d = (u16)drive << 8;
    if (mk_callg(TID_DISK, &m, gfl)) return -E_IO;
    return m.op == E_OK ? 0 : -(int)m.op;
}

static int wblk(u8 drive, u16 b, u8 write)
{
    return call(write ? DISK_WRITE : DISK_READ, drive, b, blk, BLKSIZE, write ? GR_READ : GR_WRITE);
}

static void p16(u8 *b, u16 o, u16 v) { b[o] = (u8)v; b[o + 1] = (u8)(v >> 8); }

int main(int argc, char **argv)
{
    u8 drive, yes = 0, ai = 1, t, sd, spb;
    u16 ncl, fatsz, b, i, fat1, root, ncl_bad;
    int r;
    const char *label = "MK09";
    char ans[8];

    if (argc > 1 && argv[1][0] == '-' && argv[1][1] == 'y') { yes = 1; ai = 2; }
    if (argc <= ai || argc > ai + 2) { puts("usage: format [-y] drive [label]     (drive 1-4: the floppy disk; drive 0 is the system disk)"); return 1; }
    drive = (u8)atoi_(argv[ai]);
    if (argc > ai + 1) label = argv[ai + 1];
    if (drive == 0 || drive > 4) { puts("format: the drive must be 1-4 (the drive 0 is the system disk)"); return 1; }
    if (!yes) {
        printf("format: all data on drive %u will be lost.  Continue (y/n)? ", drive);
        fflush();
        if (gets_(ans, sizeof(ans)) < 0 || (ans[0] != 'y' && ans[0] != 'Y')) { puts("format: cancelled"); return 1; }
    }

    build_track();
    for (t = 0; t < 77; t++) {
        for (sd = 0; sd < 2; sd++) {
            for (i = 0; i < NSEC; i++) { trk[idpos[i]] = t; trk[idpos[i] + 1] = sd; }
            r = call(DISK_FORMAT, drive, (u16)t * 2 + sd, trk, TRACKLEN, GR_READ);
            if (r < 0) { printf("format: track %u side %u: %s\n", t, sd, strerr_(r)); return 1; }
        }
        if (t % 10 == 0) { printf("\rformatting: track %u of 77 ", t); fflush(); }
    }
    printf("\rformatting: track 77 of 77 \n");

    /* the volume: sectors per cluster 2 (of blocks), 2 FATs, 512 root entries */
    spb = SPC;
    fatsz = 1;
    for (;;) {
        ncl = (VOLBLK - 1 - NFATS * fatsz - ROOTBLK) / spb;
        i = ((ncl + 2) * 2 + 511) / 512;
        if (i <= fatsz) break;
        fatsz = i;
    }
    fat1 = PARTBLK + 1;
    root = fat1 + NFATS * fatsz;

    fill(blk, 0, BLKSIZE);              /* block 0: MBR */
    blk[0x1BE] = 0x80;
    blk[0x1BF] = 0; blk[0x1C0] = 1; blk[0x1C1] = 1;
    blk[0x1C2] = 0x06;
    blk[0x1C3] = 0xFE; blk[0x1C4] = 0xFF; blk[0x1C5] = 0xFF;
    p16(blk, 0x1C6, PARTBLK); p16(blk, 0x1C8, 0);
    p16(blk, 0x1CA, VOLBLK); p16(blk, 0x1CC, 0);
    blk[510] = 0x55; blk[511] = 0xAA;
    if ((r = wblk(drive, 0, 1)) < 0) goto err;

    fill(blk, 0, BLKSIZE);              /* the boot sector of the volume */
    blk[0] = 0xEB; blk[1] = 0x3C; blk[2] = 0x90;
    for (i = 0; i < 8; i++) blk[3 + i] = "MK09    "[i];
    p16(blk, 11, BLKSIZE);
    blk[13] = spb;
    p16(blk, 14, 1);
    blk[16] = NFATS;
    p16(blk, 17, ROOTENT);
    p16(blk, 19, VOLBLK);
    blk[21] = 0xF8;
    p16(blk, 22, fatsz);
    p16(blk, 24, 32); p16(blk, 26, 64);
    p16(blk, 28, PARTBLK); p16(blk, 30, 0);
    blk[36] = 0x80; blk[38] = 0x29;
    p16(blk, 39, 0x0900); p16(blk, 41, 0x4D4B);
    for (i = 0; i < 11; i++) blk[43 + i] = ' ';
    for (i = 0; i < 11 && label[i]; i++) blk[43 + i] = (label[i] >= 'a' && label[i] <= 'z') ? label[i] - 32 : label[i];
    for (i = 0; i < 8; i++) blk[54 + i] = "FAT16   "[i];
    blk[510] = 0x55; blk[511] = 0xAA;
    if ((r = wblk(drive, PARTBLK, 1)) < 0) goto err;

    for (i = 0; i < NFATS; i++) {       /* the FATs: the first two entries, the rest free */
        for (b = 0; b < fatsz; b++) {
            fill(blk, 0, BLKSIZE);
            if (b == 0) { blk[0] = 0xF8; blk[1] = 0xFF; blk[2] = 0xFF; blk[3] = 0xFF; }
            if ((r = wblk(drive, fat1 + i * fatsz + b, 1)) < 0) goto err;
        }
    }
    fill(blk, 0, BLKSIZE);              /* the root directory: empty */
    for (b = 0; b < ROOTBLK; b++)
        if ((r = wblk(drive, root + b, 1)) < 0) goto err;

    printf("verifying...");             /* every block of the disk is read: all the sectors of all the tracks */
    fflush();
    ncl_bad = 0;
    for (b = 0; b < TOTBLK; b++)
        if (wblk(drive, b, 0) < 0) ncl_bad++;
    if (ncl_bad) { printf("\nformat: %u blocks cannot be read\n", ncl_bad); return 1; }
    putchar('\n');
    if ((r = wblk(drive, PARTBLK, 0)) < 0) goto err;            /* read back */
    if (blk[510] != 0x55 || blk[511] != 0xAA || blk[13] != spb) { puts("format: verify failed (boot sector)"); return 1; }
    if ((r = wblk(drive, fat1, 0)) < 0) goto err;
    if (blk[0] != 0xF8 || blk[3] != 0xFF) { puts("format: verify failed (FAT)"); return 1; }
    printf("format: drive %u done: %u clusters of %u bytes (%u KB)\n", drive, ncl, (u16)(spb * 512), (u16)((ncl * (u16)spb) / 2));
    return 0;
err:
    printf("format: %s\n", strerr_(r));
    return 1;
}
