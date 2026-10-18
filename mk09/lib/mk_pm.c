/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_pm.c - part of the MK09 C library (see mklib.h): the background programs and the memory (PM) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

int mk_rpc(u8 dst, msg_t *m, u8 gfl);

/* like spawn(), but returns at once with the task id of the program, which runs in the background */
int spawn_bg(const char *block, u16 len, u16 in_h, u16 out_h)
{
    msg_t m;
    int r;
    fflush();
    m.op = PM_SPAWNBG;
    m.a = in_h;
    m.d = out_h;
    m.b = (u16)block;
    m.c = len;
    r = mk_rpc(TID_PM, &m, GR_READ);
    if (r) return r;
    return (int)m.a;
}

/* stop a program that PM has started (not the shell) */
int kill(u8 tid)
{
    msg_t m;
    m.op = PM_KILL;
    m.a = tid;
    return mk_rpc(TID_PM, &m, 0);
}

/* the transient area in pages of 256 bytes: all, free, the biggest free piece; and the programs that run */
int meminfo(u16 *total, u16 *freep, u16 *largest, u16 *procs)
{
    msg_t m;
    int r;
    m.op = PM_MEMINFO;
    r = mk_rpc(TID_PM, &m, 0);
    if (r) return r;
    *total = m.a;
    *freep = m.b;
    *largest = m.c;
    *procs = m.d;
    return 0;
}
