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
#include "pathnames.h"
#include "mm.h"

#define HDRSIZE     16
#define IOBUF       128             /* (the start-up block, ARGMAX = 128, is in the first part of it; the bigger the chunk, the fewer reads when a program is loaded) */
#define NOH         0xFF
#ifdef MK_MMU
/* (the MMU: every program has its own address space, the program area is UBASE .. UEND of it: mk09.h) */
#define TPA_SIZE    (UEND - UBASE)
#else
#define TPA_SIZE    (TPA_END - TPA_BASE)
#endif

static u8 iobuf[IOBUF];                 /* the start-up block of the program being started in the first ARGMAX bytes,
                                           then the chunks of the file while it is loaded */
#define sb iobuf
static u8 hdr[HDRSIZE];                 /* the header of the program file */
static u8 waiter[NTASK];                /* task to answer when the program exits */
static u8 regp[NTASK], regn[NTASK];     /* the pages of a program (first page, number) */
#ifdef MK09_V2
/* 2.0: a session for every terminal (line): the shell, the foreground program, the owner of the console, ... of the line cl (the line that is served) */
typedef struct sess {
    u8 x_shell;                           /* the shell (0 = not running) */
    u8 x_fg;                              /* foreground program (0 = none) */
    u8 x_owner;                           /* the task that TTY lets read the console */
    u8 x_nproc;                           /* programs running (not counting the shell) */
    u8 x_respawn;                         /* start the shell again when everything is done */
    u8 x_uid, x_gid;                        /* the user of the shell (kept when the shell is started again) */
    u8 x_last;                            /* the task number of the last shell that ended */
    u8 x_on;                              /* a login runs on this line (the line 0, and the lines of /etc/ttys) */
} sess_t;
static sess_t S[NLINE];
static u8 cl;
static u8 t_line[NTASK];                /* the line of each task */
/* (the session of the line cl as a pointer from a function: `t == S[cl].x_shell` is compiled by CMOC with the offset in D and B loaded with t in between) */
static sess_t *SP(void) { return &S[cl]; }
#define shell_tid   SP()->x_shell
#define fg          SP()->x_fg
#define owner       SP()->x_owner
#define nproc       SP()->x_nproc
#define respawn     SP()->x_respawn
#define sh_uid      SP()->x_uid
#define sh_gid      SP()->x_gid
#define sh_last     SP()->x_last
#else
static u8 shell_tid;                    /* the shell (0 = not running) */
static u8 fg;                           /* foreground program (0 = none) */
static u8 owner;                        /* the task that TTY lets read the console */
static u8 nproc;                        /* programs running (not counting the shell) */
static u8 respawn;                      /* start the shell again when everything is done */
#endif

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

#ifdef MK_MMU
/* Level 2: MM hands out physical pages of 4KB (a list of n page numbers, written to our buffer) */
static u8 pmap[NTASK][NUP];             /* the physical pages of a program (the logical pages PG0 ..) */
static u8 pmn[NTASK];

static int mm_alloc4(u8 n, u8 *list)    /* 0 = good, -1 = no memory */
{
    msg_t m;
    m.op = MM_ALLOC;
    m.a = n;
    m.b = (u16)list;
    m.c = n;
    if (mk_callg(TID_MM, &m, GR_WRITE) || m.op) return -1;
    return 0;
}

static void mm_free4(u8 *list, u8 n)
{
    msg_t m;
    m.op = MM_FREE;
    m.a = n;
    m.b = (u16)list;
    m.c = n;
    mk_callg(TID_MM, &m, GR_READ);
}
#endif

static msg_t rm;                        /* the answer of the last call to FS */

#ifdef MK09_V2
static u8 t_uid[NTASK], t_gid[NTASK];   /* the user of each task (the effective one: a setuid file changes it) */
static u8 t_ruid[NTASK];                /* the user who started it (the real one) */
static u8 cur_ruid;
static u8 cur_dmask;
static u8 cur_uid, cur_gid;             /* the user of the program that is being started */
#endif

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

#ifdef MK09_V2
static u8 cur_parent;                   /* the task whose current directory the new program starts in (0: the root) */

static void fs_user(u8 tid, u8 u, u8 g, u8 cwd)  /* tell FS the user of a task (cwd: the task whose current directory it takes over; 0: the root; 0xFF: keep it) */
{
    msg_t m;
    m.op = FS_SETUSER;
    m.a = tid;
    m.b = u;
    m.c = g;
    m.d = cwd;
    mk_call(TID_FS, &m);
}
#endif

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
#ifdef MK09_V2
    m.c = cl;
#endif
    mk_call(TID_TTY, &m);
}

#ifdef MK09_V2
static void tty_lineon(u8 line)         /* the line is in use: TTY turns its interrupt on */
{
    msg_t m;
    m.op = TTY_IOCTL;
    m.a = TTYC_LINEON;
    m.c = line;
    mk_call(TID_TTY, &m);
}

static void tty_line(u8 tid, u8 line)   /* the task uses the console of the line */
{
    msg_t m;
    m.op = TTY_IOCTL;
    m.a = TTYC_SETLINE;
    m.b = tid;
    m.c = line;
    mk_call(TID_TTY, &m);
}
#endif

/* ------------------------------------------------------------------ */
/* start a program; the arguments are in sb[]; 'who' answers when it exits (0: nobody) */
static u8 start_program(u8 who, u8 is_shell, u8 *tidp)
{
    tcreate_t tc;
    u16 size, body, extra, n, off, base, hi, v[3], rem;
    int pg;
    u8 st, h, hin, hout, pages, i, tid = 0, created = 0;
#ifdef MK_MMU
    u8 n4, pl[NUP];
#endif

#ifdef MK09_V2
    fs_user(TID_PM, cur_uid, cur_gid, cur_parent);    /* (and a relative name of the program is looked for in the directory of the one who asks) */              /* FS checks r and x for the user who starts the program */
#endif
    rm.op = FS_OPEN;                                /* (the path is in the start-up block) */
    rm.a = FO_READ
#ifdef MK09_V2
        | FO_EXEC
#endif
        ;
    rm.b = (u16)(sb + SB_ARGS);
    rm.c = (u16)strlen((char *)sb + SB_ARGS) + 1;
    st = mk_callg(TID_FS, &rm, GR_READ);
    if (!st) st = rm.op;
#ifdef MK09_V2
    fs_user(TID_PM, 0, 0, 0xFF);
#endif
    if (st) return st;
    h = (u8)rm.a;
    hin = sb[SB_IN];
    hout = sb[SB_OUT];
    st = fsc(FS_FSTAT, h, 0);
#ifdef MK09_V2
    if (!st && (rm.d & 0x100)) cur_uid = (u8)rm.d;      /* a file with the setuid bit runs as its owner */
    cur_dmask = DMASK_USER | ((!st && (rm.d & 0x200)) ? DMASK_DISK : 0);       /* the tasks that the program may send to (the rawio bit: the disk too) */
#endif
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

#ifdef MK_MMU
    n4 = (u8)((pages + (1 << PGSH) - 1) >> PGSH);
    pg = mm_alloc4(n4, pl);
    if (pg < 0 && !is_shell && shell_tid && who == shell_tid) {     /* the shell steps aside */
        mk_tkill(shell_tid, 0);
        collect();
        respawn = 1;
        who = 0;
        pg = mm_alloc4(n4, pl);
    }
    if (pg < 0) { st = E_NOMEM; goto out; }
    base = UBASE;
    for (i = 0; i < NMAP; i++) tc.map[i] = 0;
#ifdef MK09_V2
    tc.dmask = cur_dmask;
#endif
    for (i = 1; i < PG0; i++) tc.map[i] = i;                   /* (the pages of the kernel are the same in every map) */
    for (i = 0; i < n4; i++) tc.map[PG0 + i] = pl[i];
#else
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
#endif
    hi = base + ((u16)pages << 8);
    tc.entry = base + (HW(6) - HW(4));
    tc.lo = base;
    tc.hi = hi;
    tc.top = hi - ARGMAX;
    tc.arg = hi - ARGMAX;
    tc.prio = PRIO_USER;
    st = mk_tcreate(&tc, &tid);
#ifdef MK_MMU
    if (st) { mm_free4(pl, n4); goto out; }
    created = 1;
    for (i = 0; i < n4; i++) pmap[tid][i] = pl[i];
    pmn[tid] = n4;
    regn[tid] = n4;
#else
    if (st) { mm_free((u8)pg, pages); goto out; }
    created = 1;
    regp[tid] = (u8)pg;
    regn[tid] = pages;
#endif
    st = mk_tload(tid, hi - ARGMAX, sb, ARGMAX);    /* (the buffer is the start-up block until now) */
    if (!st) st = mk_tload(tid, base, hdr, HDRSIZE);
    if (!st) {                                      /* the rest of the file goes straight from FS to the memory of the program (FS_READTL) */
        rm.op = FS_READTL;
        rm.a = h;
        rm.b = base + HDRSIZE;
        rm.c = size - HDRSIZE;
        rm.d = tid;
        st = mk_call(TID_FS, &rm);
        if (!st) st = rm.op;
        if (!st && rm.a != size - HDRSIZE) st = E_FORMAT;
    }
    if (st) goto out;
#ifdef MK09_V2
    t_line[tid] = cl;
    tty_line(tid, cl);
    t_uid[tid] = cur_uid;
    t_ruid[tid] = cur_ruid;
    t_gid[tid] = cur_gid;
    fs_user(tid, cur_uid, cur_gid, cur_parent);
#endif
    if (hin != NOH) fsc(FS_CHOWN, hin, tid);
    if (hout != NOH) fsc(FS_CHOWN, hout, tid);
    st = mk_tstart(tid);
out:
    fsc(FS_CLOSE, h, 0);
    if (st) {
        if (created) {
            mk_tkill(tid, 255);
#ifdef MK_MMU
            mm_free4(pmap[tid], pmn[tid]);
#else
            mm_free(regp[tid], regn[tid]);
#endif
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
    strcpy((char *)sb + SB_ARGS, PN_SH_MX);
#ifdef MK09_V2
    if (!respawn) strcpy((char *)sb + SB_ARGS, PN_LOGIN_MX);       /* a new session starts with the login (root); the shell that stepped aside comes back as it was */
#endif
    if (respawn) {
        sb[SB_ARGC] = 2;
        strcpy((char *)sb + SB_ARGS + 11, "-r");
    }
#ifdef MK09_V2
    cur_parent = respawn ? sh_last : 0;                 /* (the shell that stepped aside comes back in its directory) */
    cur_uid = cur_ruid = sh_uid;
    cur_gid = sh_gid;
    if (!respawn) cur_uid = cur_ruid = cur_gid = 0;
#endif
    st = start_program(0, 1, &tid);
    if (st) {
        say(PN_NO_SH);
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
#ifdef MK09_V2
    u8 c0 = cl;
#endif
    while (!mk_treap(&t, &c)) {
#ifdef MK09_V2
        cl = t_line[t];
#endif
        fsc(FS_CLOSEALL, t, 0);
#ifdef MK_MMU
        if (regn[t]) { mm_free4(pmap[t], pmn[t]); regn[t] = 0; }
#else
        if (regn[t]) { mm_free(regp[t], regn[t]); regn[t] = 0; }
#endif
        w = waiter[t];
        waiter[t] = 0;
        if (t == shell_tid) {
#ifdef MK09_V2
            sh_uid = t_ruid[t];
            sh_last = t;
            sh_gid = t_gid[t];
#endif
            shell_tid = 0;
        } else {
            if (nproc) nproc--;
            if (fg == t) fg = 0;
            if (w) reply(w, E_OK, c);
        }
    }
#ifdef MK09_V2
    for (cl = 0; cl < NLINE; cl++) set_owner();
    cl = c0;
#else
    set_owner();
#endif
}

static void reap(void)
{
    collect();
#ifdef MK09_V2
    for (cl = 0; cl < NLINE; cl++)
        if (SP()->x_on && !shell_tid && !nproc) start_shell();
    cl = 0;
#else
    if (!shell_tid && !nproc) start_shell();
#endif
}

/* PM_SPAWN (bg = 0: the answer comes when the program has exited), PM_SPAWNBG (bg = 1: the answer, the task id, comes at once) and PM_EXEC (bg = 2:
 * the program that asks is replaced by the new one, which is waited for by the task that waited for the old one and has its console; the memory of
 * the old one is given back first, so that a program can start a bigger one: the stages of the C compiler follow each other like this) */
static void do_spawn(msg_t *m, u8 bg)
{
#ifdef MK09_V2
    u8 tid, st, i, w = m->src, wasfg, sh;
#else
    u8 tid, st, i, w = m->src, wasfg = (fg == w);
#endif
    u16 n = m->c;
#ifdef MK09_V2
    cl = 0;                                                     /* (not ?: here: CMOC and the types of the two sides) */
    if (m->src < NTASK) cl = t_line[m->src];                    /* the program runs on the line of the one who asks */
    wasfg = (fg == w);
    sh = (bg == 2 && m->src == shell_tid);                      /* the login starts the shell of the user: the new program is the shell */
#endif

    if (n < 2 || n > ARGMAX - SB_ARGS - 1) { reply(m->src, E_INVAL, 0); return; }
    sb_new((m->a == NOHANDLE) ? NOH : (u8)m->a, (m->d == NOHANDLE) ? NOH : (u8)m->d);
    if (mk_copyfrom(m->src, 0, sb + SB_ARGS, n)) { reply(m->src, E_FAULT, 0); return; }
    sb[SB_ARGS + n] = 0;
    sb[SB_ARGS + n - 1] = 0;
    for (i = SB_ARGS; i < SB_ARGS + n; i++)                     /* count the arguments */
        if (sb[i] == 0 && sb[i + 1] != 0) sb[SB_ARGC]++;
    sb[SB_ARGC]++;
#ifdef MK09_V2
    cur_ruid = t_ruid[m->src];
    cur_parent = m->src;
    cur_uid = t_uid[m->src];                                    /* the new program is run by the user who asks (PM_EXEC: the task that is replaced) */
    cur_gid = t_gid[m->src];
#endif
    if (bg == 2) {
        w = waiter[w];
        if (sb[SB_IN] != NOH) fsc(FS_CHOWN, sb[SB_IN], TID_PM);     /* (the files of the redirection must not be closed with the old program) */
        if (sb[SB_OUT] != NOH) fsc(FS_CHOWN, sb[SB_OUT], TID_PM);
        waiter[m->src] = 0;
        mk_tkill(m->src, 0);
        collect();
    }
#ifdef MK09_V2
    st = start_program(bg == 1 ? 0 : w, sh, &tid);
    if (st && sh) { start_shell(); return; }                    /* (the login starts again) */
    if (!st && sh) { set_owner(); return; }
#else
    st = start_program(bg == 1 ? 0 : w, 0, &tid);
#endif
    if (st && !w) say("\r\nbg: cannot start\r\n");        /* (nobody waits for it: a stage of  pc &  that does not fit in the memory) */
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
#ifdef MK09_V2
        if (t != S[t_line[t]].x_shell && (t_uid[m->src] == 0 || t_uid[m->src] == t_uid[t])) { mk_tkill(t, 143); st = E_OK; }   /* the same user, or root */
#else
        if (t != shell_tid) { mk_tkill(t, 143); st = E_OK; }
#endif
    }
    reply(m->src, st, 0);
}

#ifdef MK09_V2
/* /etc/ttys: the digits in it are the lines (terminals) that get a login besides the line 0 */
static void read_ttys(void)
{
    u8 h, i;
    S[0].x_on = 1;
    strcpy((char *)sb + SB_ARGS, PN_TTYS);
    rm.op = FS_OPEN;
    rm.a = FO_READ;
    rm.b = (u16)(sb + SB_ARGS);
    rm.c = (u16)strlen((char *)sb + SB_ARGS) + 1;
    if (mk_callg(TID_FS, &rm, GR_READ) || rm.op) return;
    h = (u8)rm.a;
    if (!fs_rd(h, hdr, 16)) {
        for (i = 0; i < (u8)rm.a && i < 16; i++)
            if (hdr[i] > '0' && hdr[i] < '0' + NLINE) { S[hdr[i] - '0'].x_on = 1; tty_lineon(hdr[i] - '0'); }
    }
    fsc(FS_CLOSE, h, 0);
}
#endif

int main(void)
{
    msg_t m;
    u8 c;
#ifdef MK09_V2
    u8 i;
#endif

    say("MK09 " MK09_VERSION MK09_FOR ": kernel " MK09_KERNEL_VERSION " TTY DISK FS PM MM\r\n");
    if (fsc(FS_SYNC, 0, 0)) say("MK09: no FAT16 volume\r\n");        /* the first FS call mounts the volume */
#ifdef MK09_V2
    read_ttys();
    for (cl = 0; cl < NLINE; cl++)
        if (SP()->x_on) start_shell();
    cl = 0;
#else
    start_shell();
#endif
    for (;;) {
        if (mk_recv(TID_ANY, &m)) continue;
        if (m.src == TID_HW) {
            if (m.a & NFY_EXIT) reap();
#ifdef MK09_V2
            if ((m.a & NFY_INT) && S[0].x_fg) mk_tkill(S[0].x_fg, 130);
#if NLINE > 1
            if ((m.a & NFY_INT1) && S[1].x_fg) mk_tkill(S[1].x_fg, 130);
#endif
#else
            if ((m.a & NFY_INT) && fg) mk_tkill(fg, 130);
#endif
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
#ifdef MK09_V2
            m.d = 0;
            for (i = 0; i < NLINE; i++) m.d += S[i].x_nproc;       /* (not c: it has the task to answer) */
#else
            m.d = nproc;
#endif
            mk_reply(c, &m);
            break;
#ifdef MK09_V2
        case PM_SETUSER:                                /* root changes the user of the task that asks */
            if (m.src >= NTASK || t_uid[m.src] != 0) { reply(m.src, E_PERM, 0); break; }
            t_uid[m.src] = t_ruid[m.src] = (u8)m.a;
            t_gid[m.src] = (u8)m.b;
            fs_user(m.src, (u8)m.a, (u8)m.b, 0xFF);
            reply(m.src, E_OK, 0);
            break;
        case PM_GETUSER:
            c = m.src;
            m.a = t_uid[c];
            m.b = t_gid[c];
            m.c = t_ruid[c];
            m.op = E_OK;
            mk_reply(c, &m);
            break;
#endif
        case PM_SHUTDOWN:
#ifdef MK09_V2
            if (t_uid[m.src] != 0) { reply(m.src, E_PERM, 0); break; }      /* only root stops the machine */
#endif
            fsc(FS_SYNC, 0, 0);
            say("CPU halted.\r\n");
            mk_power();
            break;
        default:
            reply(m.src, E_NOSYS, 0);
        }
    }
    return 0;
}
