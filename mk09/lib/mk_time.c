/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_time.c - part of the MK09 C library (see mklib.h); one object per group so that a program links only what it uses */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

extern u8 mk_in_h, mk_out_h;
#define in_h mk_in_h
#define out_h mk_out_h
#define NOFD 0xFF
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

int settime(u16 date, u16 time)
{
    msg_t m;
    u8 y = date >> 9, mo = (date >> 5) & 15, d = date & 31, mi = (time >> 5) & 63, sec = (time & 31) << 1, len = (u8)(30 + ((mo ^ (mo >> 3)) & 1));
    if (mo == 2) len = (y & 3) ? 28 : 29;
    if (mo < 1 || mo > 12 || d < 1 || d > len || (time >> 11) > 23 || mi > 59 || sec > 58) return -E_INVAL;
    m.op = FS_STIME;
    m.a = ((u16)y << 8) | mo;
    m.b = ((u16)d << 8) | (time >> 11);
    m.c = ((u16)mi << 8) | sec;
    return mk_rpc(TID_FS, &m, 0);
}

int gettime(u16 *date, u16 *time)
{
    msg_t m;
    int r;
    m.op = FS_TIME;
    r = mk_rpc(TID_FS, &m, 0);
    if (r) return r;
    *date = m.a;
    *time = m.b;
    return 0;
}

