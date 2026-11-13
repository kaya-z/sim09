/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* df - space of the file system */
#include "types.h"
#include "mklib.h"

static u16 amount(unsigned long kb, const char **unit)       /* KB, or MB when it is 64MB or more (the volume has up to 2GB) */
{
    if (kb >= 65536UL) { *unit = "MB"; return (u16)(kb >> 10); }
    *unit = "KB";
    return (u16)kb;
}

int main(int argc, char **argv)
{
    u8 spc;
    u16 total, freec, f, t;
    const char *fu, *tu;
    u8 drive = 0;
    int r;
    if (argc > 1 && argv[1][0] >= '0' && argv[1][0] <= '4') drive = (u8)(argv[1][0] - '0');      /* df 1: the floppy disk (FM-11) */
    r = statfs_drv(drive, &spc, &total, &freec);
    if (r < 0) { printf("df: %s\n", strerr_(r)); return 1; }
    f = amount(((unsigned long)freec * spc) / 2, &fu);
    t = amount(((unsigned long)total * spc) / 2, &tu);
    printf("cluster %u bytes, %u clusters, %u free (%u %s free of %u %s)\n", (u16)(spc * 512), total, freec, f, fu, t, tu);
    return 0;
}
