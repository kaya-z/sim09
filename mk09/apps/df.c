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

int main(void)
{
    u8 spc;
    u16 total, freec, f, t;
    const char *fu, *tu;
    int r = statfs(&spc, &total, &freec);
    if (r < 0) { printf("df: %s\n", strerr_(r)); return 1; }
    f = amount(((unsigned long)freec * spc) / 2, &fu);
    t = amount(((unsigned long)total * spc) / 2, &tu);
    printf("cluster %u bytes, %u clusters, %u free (%u %s free of %u %s)\n", (u16)(spc * 512), total, freec, f, fu, t, tu);
    return 0;
}
