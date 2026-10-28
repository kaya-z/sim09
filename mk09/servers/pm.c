/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pm.c - the process manager (TASK 4)
 *
 * Starts programs (MX files from the file system) and waits for them: the shell asks for
 * PM_SPAWN and gets the answer when the program has exited, or for PM_SPAWNBG and gets it at once
 * (the program runs in the background).  Several programs can run at the same time.  PM also
 *   - asks the memory manager (mm.c) for the memory of a program: the programs are position
 *     independent (MX version 2, see lib/crt0app.s), so they can be loaded at any page,
 *   - starts the shell when the system comes up and again when it has gone,
 *   - takes the shell out of memory when a program needs its space and loads it again when the
 *     programs are done,
 *   - tells the console (TTY) which program owns the keyboard: the foreground program, or the
 *     shell; the other programs get E_BUSY when they read, and ^C stops the foreground program only,
 *   - closes the files of exited programs (FS_CLOSEALL), gives the memory back,
 *   - kills a program (PM_KILL), reports the memory (PM_MEMINFO), tells the time (PM_TIME) and
 *     powers the machine off (PM_SHUTDOWN).
 *
 * The program file is the 16 byte header (lib/crt0app.s), the image, and the relocation table.
 * The image is loaded at the first byte of the program's memory, the table follows it (it is
 * where the bss begins and is overwritten when the program has relocated itself), then come
 * the stack and the start-up block (mklib.h) with the arguments at the end.
 */
#include "types.h"
#include "mk09.h"
#include "proto.h"
#include "mklib.h"
#include "mkstr.h"
#include "memmap.h"
#include "version.h"
#include "mm.h"

#define HDRSIZE     16
#define IOBUF       224             /* (the start-up block, ARGMAX = 128, is in the first part of it; the bigger the chunk, the fewer reads when a program is loaded) */
#define NOH         0xFF
#define TPA_SIZE    (TPA_END - TPA_BASE)

static u8 iobuf[IOBUF];                 /* the start-up block of the program being started in the first ARGMAX bytes,
                                           then the chunks of the file while it is loaded */
#define sb iobuf
static u8 hdr[HDRSIZE];                 /* the header of the program file */
static u8 waiter[NTASK];                /* task to answer when the program exits */
static u8 regp[NTASK], regn[NTASK];     /* the pages of a program (first page, number) */
static u8 shell_tid;                    /* the shell (0 = not running) */
static u8 fg;                           /* foreground program (0 = none) */
static u8 owner;                        /* the task that TTY lets read the console */
static u8 nproc;                        /* programs running (not counting the shell) */
static u8 respawn;                      /* start the shell again when everything is done */

/* the memory manager is a task of its own (mm.s): PM asks it for the pages of a program and gives them back */
static int mm_alloc(u8 n, u8 top)                   /* the first page of n pages in a row, or -1 */
{
    msg_t m;
    m.op = MM_ALLOC;
    m.a = n;
    m.d = top;
    if (mk_call(TID_MM, &m) || m.op) return -1;
    return (int)m.a;
}

static void mm_free(u8 first, u8 n)
{
    msg_t m;
    m.op = MM_FREE;
    m.a = first;
    m.b = n;
    mk_call(TID_MM, &m);
}

static msg_t rm;                        /* the answer of the last call to FS */

static u8 fsc(u8 op, u16 a, u16 b)      /* FS request without a buffer */
{
    u8 st;
    rm.op = op;
    rm.a = a;
    rm.b = b;
    st = mk_call(TID_FS, &rm);
    if (st) return st;
    return rm.op;
}

static u8 fs_rd(u8 h, void *buf, u16 n)  /* FS_READ into buf */
{
    u8 st;
    rm.op = FS_READ;
    rm.a = h;
    rm.b = (u16)buf;
    rm.c = n;
    st = mk_callg(TID_FS, &rm, GR_WRITE);
    if (st) return st;
    return rm.op;
}

#define HW(i)   (*(u16 *)(hdr + (i)))        /* a word of the header (the 6809 is big endian like the file) */

static void say(const char *s)         /* (a grant must be in the data of this task, not in its code) */
{
    msg_t m;
    strcpy((char *)iobuf, s);
    m.op = TTY_WRITE;
    m.b = (u16)iobuf;
    m.c = (u16)strlen(s);
    mk_callg(TID_TTY, &m, GR_READ);
}

static void reply(u8 tid, u8 st, u16 a)
{
    msg_t r;
    r.op = st;
    r.a = a;
    r.b = r.c = r.d = 0;
    mk_reply(tid, &r);
}

static void collect(void);

/* tell TTY who may read the console: the foreground program, or else the shell */
static void set_owner(void)
{
    msg_t m;
    u8 o = fg ? fg : shell_tid;
    if (o == owner) return;
    owner = o;
    m.op = TTY_IOCTL;
    m.a = TTYC_SETFG;
    m.b = o;
    mk_call(TID_TTY, &m);
}

/* ------------------------------------------------------------------ */
/* start a program; the arguments are in sb[]; 'who' answers when it exits (0: nobody) */
static u8 start_program(u8 who, u8 is_shell, u8 *tidp)
{
    tcreate_t tc;
    u16 size, body, extra, n, off, base, hi, v[3], rem;
    int pg;
    u8 st, h, hin, hout, pages, i, tid = 0, created = 0;

    rm.op = FS_OPEN;                                /* (the path is in the start-up block) */
    rm.a = FO_READ;
    rm.b = (u16)(sb + SB_ARGS);
    rm.c = (u16)strlen((char *)sb + SB_ARGS) + 1;
    st = mk_callg(TID_FS, &rm, GR_READ);
    if (!st) st = rm.op;
    if (st) return st;
    h = (u8)rm.a;
    hin = sb[SB_IN];
    hout = sb[SB_OUT];
    st = fsc(FS_FSTAT, h, 0);
    size = rm.b;
    if (!st && rm.c) st = E_FORMAT;
    if (!st) st = fs_rd(h, hdr, HDRSIZE);
    if (st) goto out;
    if (rm.a < HDRSIZE || HW(0) != 0x4D4B || hdr[2] != MX_VERSION) { st = E_FORMAT; goto out; }
    body = HW(8);
    if (body < HDRSIZE || body > size) { st = E_FORMAT; goto out; }
    extra = size - body;                            /* the relocation table is where the bss begins */
    v[0] = body;
    v[1] = HW(10);                          /* the bss */
    if (v[1] < extra) v[1] = extra;
    v[2] = HW(12);                          /* the stack */
    rem = TPA_SIZE - ARGMAX;                        /* what is left of the area; nothing may wrap around */
    for (i = 0; i < 3; i++) {
        if (v[i] > rem) { st = E_NOMEM; goto out; }
        rem -= v[i];
    }
    pages = (u8)((TPA_SIZE - rem + 255) >> 8);

    pg = mm_alloc(pages, is_shell);
    if (pg < 0 && !is_shell && shell_tid && who == shell_tid) {     /* the shell steps aside */
        mk_tkill(shell_tid, 0);
        collect();
        respawn = 1;
        who = 0;
        pg = mm_alloc(pages, 0);
    }
    if (pg < 0) { st = E_NOMEM; goto out; }
    base = MM_ADDR(pg);
    hi = base + ((u16)pages << 8);
    tc.entry = base + (HW(6) - HW(4));
    tc.lo = base;
    tc.hi = hi;
    tc.top = hi - ARGMAX;
    tc.arg = hi - ARGMAX;
    tc.prio = PRIO_USER;
    st = mk_tcreate(&tc, &tid);
    if (st) { mm_free((u8)pg, pages); goto out; }
    created = 1;
    regp[tid] = (u8)pg;
    regn[tid] = pages;
    st = mk_tload(tid, hi - ARGMAX, sb, ARGMAX);    /* (the buffer is the start-up block until now) */
    if (!st) st = mk_tload(tid, base, hdr, HDRSIZE);
    off = HDRSIZE;
    while (!st && off < size) {
        st = fs_rd(h, iobuf, IOBUF);
        if (st) break;
        n = rm.a;
        if (!n) { st = E_FORMAT; break; }
        if (off + n > size) n = size - off;
        st = mk_tload(tid, base + off, iobuf, n);
        off += n;
    }
    if (st) goto out;
    if (hin != NOH) fsc(FS_CHOWN, hin, tid);
    if (hout != NOH) fsc(FS_CHOWN, hout, tid);
    st = mk_tstart(tid);
out:
    fsc(FS_CLOSE, h, 0);
    if (st) {
        if (created) {
            mk_tkill(tid, 255);
            mm_free(regp[tid], regn[tid]);
            regn[tid] = 0;
        }
        return st;
    }
    waiter[tid] = who;
    if (is_shell) shell_tid = tid;
    else nproc++;
    *tidp = tid;
    return E_OK;
}

static void sb_new(u8 in, u8 out)           /* an empty start-up block with the standard files in and out */
{
    memset(sb, 0, ARGMAX);
    sb[0] = SB_VERSION;
    sb[SB_IN] = in;
    sb[SB_OUT] = out;
}

static void start_shell(void)
{
    u8 tid;
    u8 st;
    sb_new(NOH, NOH);
    sb[SB_ARGC] = 1;
    strcpy((char *)sb + SB_ARGS, "/BIN/SH.MX");
    if (respawn) {
        sb[SB_ARGC] = 2;
        strcpy((char *)sb + SB_ARGS + 11, "-r");
    }
    st = start_program(0, 1, &tid);
    if (st) {
        say("MK09: no SH.MX\r\n");
        mk_sleep(250);
    } else {
        respawn = 0;
        set_owner();
    }
}

/* handle everything that has exited */
static void collect(void)
{
    u8 t, c, w;
    while (!mk_treap(&t, &c)) {
        fsc(FS_CLOSEALL, t, 0);
        if (regn[t]) { mm_free(regp[t], regn[t]); regn[t] = 0; }
        w = waiter[t];
        waiter[t] = 0;
        if (t == shell_tid) {
            shell_tid = 0;
        } else {
            if (nproc) nproc--;
            if (fg == t) fg = 0;
            if (w) reply(w, E_OK, c);
        }
    }
    set_owner();
}

static void reap(void)
{
    collect();
    if (!shell_tid && !nproc) start_shell();
}

/* PM_SPAWN (bg = 0: the answer comes when the program has exited), PM_SPAWNBG (bg = 1: the answer, the task id, comes at once) and PM_EXEC (bg = 2:
 * the program that asks is replaced by the new one, which is waited for by the task that waited for the old one and has its console; the memory of
 * the old one is given back first, so that a program can start a bigger one: the stages of the C compiler follow each other like this) */
static void do_spawn(msg_t *m, u8 bg)
{
    u8 tid, st, i, w = m->src, wasfg = (fg == w);
    u16 n = m->c;

    if (n < 2 || n > ARGMAX - SB_ARGS - 1) { reply(m->src, E_INVAL, 0); return; }
    sb_new((m->a == NOHANDLE) ? NOH : (u8)m->a, (m->d == NOHANDLE) ? NOH : (u8)m->d);
    if (mk_copyfrom(m->src, 0, sb + SB_ARGS, n)) { reply(m->src, E_FAULT, 0); return; }
    sb[SB_ARGS + n] = 0;
    sb[SB_ARGS + n - 1] = 0;
    for (i = SB_ARGS; i < SB_ARGS + n; i++)                     /* count the arguments */
        if (sb[i] == 0 && sb[i + 1] != 0) sb[SB_ARGC]++;
    sb[SB_ARGC]++;
    if (bg == 2) {
        w = waiter[w];
        if (sb[SB_IN] != NOH) fsc(FS_CHOWN, sb[SB_IN], TID_PM);     /* (the files of the redirection must not be closed with the old program) */
        if (sb[SB_OUT] != NOH) fsc(FS_CHOWN, sb[SB_OUT], TID_PM);
        waiter[m->src] = 0;
        mk_tkill(m->src, 0);
        collect();
    }
    st = start_program(bg == 1 ? 0 : w, 0, &tid);
    if (st) { reply(w, st, 0); return; }
    if (bg == 1) {
        reply(m->src, E_OK, tid);
    } else if (bg == 0 || wasfg) {
        fg = tid;
        set_owner();
    }
}

static void do_kill(msg_t *m)
{
    u8 t = (u8)m->a, st = E_NOTASK;
    if (m->a < NTASK && t >= TID_USER0 && regn[t]) {
        st = E_PERM;
        if (t != shell_tid) { mk_tkill(t, 143); st = E_OK; }
    }
    reply(m->src, st, 0);
}

int main(void)
{
    msg_t m;
    u8 c;

    say("MK09 " MK09_VERSION ": kernel TTY DISK FS PM MM\r\n");
    if (fsc(FS_SYNC, 0, 0)) say("MK09: no FAT16 volume\r\n");        /* the first FS call mounts the volume */
    start_shell();
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) {
            if (m.a & NFY_EXIT) reap();
            if ((m.a & NFY_INT) && fg) mk_tkill(fg, 130);
            continue;
        }
        switch (m.op) {
        case PM_SPAWN:
            do_spawn(&m, 0);
            break;
        case PM_SPAWNBG:
            do_spawn(&m, 1);
            break;
        case PM_KILL:
            do_kill(&m);
            break;
        case PM_EXEC:
            do_spawn(&m, 2);
            break;
        case PM_MEMINFO:                                /* the answer of MM, and the programs that run */
            c = m.src;
            m.op = MM_INFO;
            if (mk_call(TID_MM, &m)) m.op = E_NOTASK;
            m.d = nproc;
            mk_reply(c, &m);
            break;
        case PM_SHUTDOWN:
            fsc(FS_SYNC, 0, 0);
            mk_power();
            break;
        default:
            reply(m.src, E_NOSYS, 0);
        }
    }
    return 0;
}
