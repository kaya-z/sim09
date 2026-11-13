/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_mount.c - part of the MK09 C library (see mklib.h): mount and unmount the volume of a drive (FM-11 floppy disks) */
#include "types.h"
#include "mklib.h"

int mk_rpc(u8 dst, msg_t *m, u8 gfl);

static int fsm(u8 drive, u8 what, u16 *a)
{
    msg_t m;
    int r;
    m.op = FS_MOUNT;
    m.a = drive;
    m.b = what;
    r = mk_rpc(TID_FS, &m, 0);
    if (r) return r;
    if (a) *a = m.a;
    return 0;
}

int fsmount(u8 drive) { return fsm(drive, 0, 0); }
int fsumount(u8 drive) { return fsm(drive, 1, 0); }

int fsmounted(u8 drive)
{
    u16 a;
    int r = fsm(drive, 2, &a);
    return r < 0 ? r : (int)a;
}
