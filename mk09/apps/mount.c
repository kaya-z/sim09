/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mount / umount - the floppy disks of the FM-11 (BOARD=fm11)
 *
 *     mount            the drives and what is mounted
 *     mount N          mount the disk in the drive N (1-4); its files are named  N:/DIR/FILE  (cp, ls, cat ... take such names)
 *     umount N         unmount it (not while a file of it is open); change the disk only after that
 *
 * The drive 0 is the system disk and always mounted.  umount.c is this file compiled with -DUMOUNT.
 */
#include "types.h"
#include "mklib.h"
#define TOOL_VERSION "1.0"

static int show(u8 d)
{
    u8 spc;
    u16 total, freec;
    int r = statfs_drv(d, &spc, &total, &freec);
    if (r < 0) return r;
    printf("%u: %u clusters of %u bytes, %u free (%u KB of %u KB)\n", d, total, (u16)(spc * 512), freec,
           (u16)(((unsigned long)freec * spc) / 2), (u16)(((unsigned long)total * spc) / 2));
    return 0;
}

int main(int argc, char **argv)
{
#ifdef UMOUNT
    u8 d, um = 1;
#else
    u8 d, um = 0;
#endif
    int r;

    if (argc == 1 && !um) {
        for (d = 0; d <= 4; d++) {
            if (d == 0 || fsmounted(d) == 1) {
                if (show(d) < 0) printf("%u: system disk\n", d);
            } else printf("%u: not mounted\n", d);
        }
        return 0;
    }
    if (argc != 2 || argv[1][0] < '1' || argv[1][0] > '4' || argv[1][1]) {
        puts(um ? "usage: umount drive    (drive 1-4)" : "usage: mount [drive]    (drive 1-4)");
        return 1;
    }
    d = (u8)(argv[1][0] - '0');
    if (um) {
        r = fsumount(d);
        if (r < 0) { printf("umount: %s\n", strerr_(r)); return 1; }
        printf("umount: drive %u is free\n", d);
        return 0;
    }
    r = fsmount(d);
    if (r < 0) { printf("mount: drive %u: %s\n", d, strerr_(r)); return 1; }
    show(d);
    printf("mount: the files of the drive %u are %u:/NAME\n", d, d);
    return 0;
}
