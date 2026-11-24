/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_user.c - part of the MK09 C library (see mklib.h): users, owners and permissions (2.0) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#ifdef MK09_V2
int mk_rpc(u8 dst, msg_t *m, u8 gfl);
int mk_rpc_path(u8 op, const char *path, u16 a, msg_t *m);

/* the user and the group of this program */
int getuser(u8 *uid, u8 *gid)
{
    msg_t m;
    int r;
    m.op = PM_GETUSER;
    r = mk_rpc(TID_PM, &m, 0);
    if (r) return r;
    *uid = (u8)m.a;
    *gid = (u8)m.b;
    return 0;
}

/* the terminal and the user of a task (PM); *run: 1 for a program that PM runs (not a server) */
int taskuser(u8 tid, u8 *line, u8 *uid, u8 *run)
{
    msg_t m;
    int r;
    m.op = PM_TASKINFO;
    m.a = tid;
    r = mk_rpc(TID_PM, &m, 0);
    if (r) return r;
    *line = (u8)m.a;
    *uid = (u8)m.b;
    *run = (u8)m.c;
    return 0;
}

/* change the user of this program (root only; then start the shell or a program with exec) */
int setuser(u8 uid, u8 gid)
{
    msg_t m;
    m.op = PM_SETUSER;
    m.a = uid;
    m.b = gid;
    return mk_rpc(TID_PM, &m, 0);
}

/* the permissions of a file or directory (the owner or root; mode 01777) */
int chmod(const char *path, u16 mode)
{
    msg_t m;
    return mk_rpc_path(FS_CHMOD, path, mode, &m);
}

/* the owner and the group (root only); 0xFFFF: not changed */
int chown(const char *path, u16 uid, u16 gid)
{
    msg_t m;
    m.op = FS_CHUSER;
    m.a = uid;
    m.d = gid;
    m.b = (u16)path;
    m.c = (u16)strlen(path) + 1;
    return mk_rpc(TID_FS, &m, GR_READ);
}
#endif
