/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * os9shim.c - the file and process calls of the compiler tools (inc/mklib.h) over the system calls of OS-9 Level 2.
 * Errors are returned as -(the error code of OS-9).  An OS-9 path name ends with CR.
 *
 * The system calls are made by the small functions call_xxx, which read the registers from the global variables g_d g_x g_y g_u and leave them in
 * r_d r_x r_y r_cc (carry in bit 0 of r_cc: an error, the code is in B).  They use no parameter and no local variable of C: the position of those depends on
 * the options of CMOC (a frame pointer or not), and Y is the base of the global variables of an OS-9 program of CMOC, so it is saved and restored.
 */
#include <cmoc.h>
#include "mklib.h"
#undef open
#undef close
#undef read
#undef write
#undef seek
#undef unlink
#undef exec
#undef putstr
#undef puts

#define E_EOF   211
#define E_CEF   218

static char pbuf[132];          /* the path name of the call that is made now */
static char nbuf[40];           /* the name of the module of F$Fork and F$Chain */
static char xbuf[ARGMAX + 4];   /* the parameters of F$Fork and F$Chain */

/* a path name: the part that starts with /LIB/ or /INC/ is on /d0 (the MK09 programs find their library and the headers there), then CR at the end */
static char *ospath(const char *p)
{
    u16 i = 0;
    if (p[0] == '/' && (((p[1] == 'L' || p[1] == 'l') && (p[2] == 'I' || p[2] == 'i') && (p[3] == 'B' || p[3] == 'b') && p[4] == '/') ||
                        ((p[1] == 'I' || p[1] == 'i') && (p[2] == 'N' || p[2] == 'n') && (p[3] == 'C' || p[3] == 'c') && p[4] == '/'))) {
        pbuf[0] = '/'; pbuf[1] = 'd'; pbuf[2] = '0';
        i = 3;
    }
    while (*p && i < 128) pbuf[i++] = *p++;
    pbuf[i] = 13;
    return pbuf;
}

static u16 g_d, g_x, g_y, g_u;          /* the registers D X Y U before the call */
static u16 r_d, r_x, r_y;               /* ... and after it */
static u8 r_cc;
static int oserr;                       /* the error of the last call that failed */

static void call_open(void)             /* I$Open */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $84
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_create(void)             /* I$Create */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $83
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_delete(void)             /* I$Delete */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $87
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_close(void)             /* I$Close */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $8F
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_read(void)             /* I$Read */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $89
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_write(void)             /* I$Write */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $8A
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_seek(void)             /* I$Seek */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $88
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_fork(void)             /* F$Fork */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $03
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_wait(void)             /* F$Wait */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $04
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

static void call_chain(void)             /* F$Chain */
{
    asm {
        pshs y,u
        ldd  :g_d
        ldx  :g_x
        ldu  :g_u
        ldy  :g_y
        os9  $05
        pshs cc,y
        ldy  3,s
        std  :r_d
        stx  :r_x
        puls a
        sta  :r_cc
        puls x
        stx  :r_y
        puls y,u
    }
}

/* after a call: 0, or -1 with the error in oserr */
static int failed(void)
{
    if (r_cc & 1) { oserr = r_d & 255; return -1; }
    return 0;
}

static int sys_open(char *name, int mode)
{
    g_d = (u16)mode << 8; g_x = (u16)name; g_y = 0; g_u = 0;
    call_open();
    return failed() ? -1 : (int)(r_d >> 8);
}

static int sys_create(char *name, int mode, int attr)
{
    g_d = ((u16)mode << 8) | (u8)attr; g_x = (u16)name; g_y = 0; g_u = 0;
    call_create();
    return failed() ? -1 : (int)(r_d >> 8);
}

static int sys_delete(char *name)
{
    g_d = 0; g_x = (u16)name; g_y = 0; g_u = 0;
    call_delete();
    return failed();
}

static int sys_close(int path)
{
    g_d = (u16)path << 8; g_x = 0; g_y = 0; g_u = 0;
    call_close();
    return failed();
}

static int sys_rd(int path, void *buf, u16 n)
{
    g_d = (u16)path << 8; g_x = (u16)buf; g_y = n; g_u = 0;
    call_read();
    return failed() ? -1 : (int)r_y;
}

static int sys_wr(int path, void *buf, u16 n)
{
    g_d = (u16)path << 8; g_x = (u16)buf; g_y = n; g_u = 0;
    call_write();
    return failed() ? -1 : (int)r_y;
}

static int sys_seek(int path, u16 hi, u16 lo)
{
    g_d = (u16)path << 8; g_x = hi; g_y = 0; g_u = lo;
    call_seek();
    return failed();
}

/* F$Fork: the module name (CR at the end), the parameters (size n), 0 data pages: the default of the module -> the process id of the child */
static int sys_fork(char *name, char *params, u16 n)
{
    g_d = 0; g_x = (u16)name; g_y = n; g_u = (u16)params;
    call_fork();
    return failed() ? -1 : (int)(r_d >> 8);
}

/* F$Wait: wait for a child to end -> (its process id << 8) | its exit status */
static int sys_wait(void)
{
    g_d = 0; g_x = 0; g_y = 0; g_u = 0;
    call_wait();
    return failed() ? -1 : (int)r_d;
}

/* F$Chain: like F$Fork, but this process is replaced; it returns only when it fails */
static int sys_chain(char *name, char *params, u16 n)
{
    g_d = 0; g_x = (u16)name; g_y = n; g_u = (u16)params;
    call_chain();
    return failed();
}

/* ---- the interface */
int o9_open(const char *path, u8 flags)
{
    char *p = ospath(path);
    int fd;
    if (flags & FO_CREATE) {
        int mode = (flags & FO_READ) ? 3 : 2;
        int attr = (flags & FO_EXEC) ? 0x2F : 0x0B;
        fd = sys_create(p, mode, attr);
        if (fd < 0 && oserr == E_CEF) {                 /* the file exists: replace it */
            if (sys_delete(p) < 0) return -oserr;
            fd = sys_create(p, mode, attr);
        }
    } else
        fd = sys_open(p, (flags & FO_WRITE) ? ((flags & FO_READ) ? 3 : 2) : 1);
    return fd < 0 ? -oserr : fd;
}

int o9_close(int fd)
{
    return sys_close(fd) < 0 ? -oserr : 0;
}

int o9_read(int fd, void *buf, u16 n)
{
    int r = sys_rd(fd, buf, n);
    if (r >= 0) return r;
    return oserr == E_EOF ? 0 : -oserr;
}

int o9_write(int fd, const void *buf, u16 n)
{
    int r = sys_wr(fd, (void *)buf, n);
    return r < 0 ? -oserr : r;
}

int o9_seek(int fd, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo)
{
    (void)whence;
    if (sys_seek(fd, hi, lo) < 0) return -oserr;
    if (rhi) *rhi = hi;
    if (rlo) *rlo = lo;
    return 0;
}

int o9_unlink(const char *path)
{
    return sys_delete(ospath(path)) < 0 ? -oserr : 0;
}

/* "/BIN/NAME.MX" (or NAME) and the arguments: F$Chain to the module NAME with the parameters "arg arg ...", CR at the end */
int o9_exec(const char *block, u16 len)
{
    const char *p = block;
    u16 i = 0, k = 0;
    (void)len;
    if (p[0] == '/' && p[1] == 'B' && p[2] == 'I' && p[3] == 'N' && p[4] == '/') p += 5;
    while (*p && *p != '.' && i < 36) nbuf[i++] = *p++;
    nbuf[i] = 13;
    while (*p) p++;
    p++;                                                 /* the first argument */
    while (*p && k + 2 < ARGMAX) {
        if (k) xbuf[k++] = ' ';
        while (*p && k + 2 < ARGMAX) xbuf[k++] = *p++;
        p++;
    }
    xbuf[k++] = 13;
    sys_chain(nbuf, xbuf, k);
    return -oserr;
}

/* run the program argv[0] with the arguments argv[1..] and wait for it: its exit status, or -error */
int o9_run(int argc, char **argv)
{
    u16 i, k = 0;
    int pid, w;
    for (i = 0; argv[0][i] && i < 36; i++) nbuf[i] = argv[0][i];
    nbuf[i] = 13;
    for (i = 1; i < argc; i++) {
        const char *p = argv[i];
        if (k) xbuf[k++] = ' ';
        while (*p && k + 2 < ARGMAX) xbuf[k++] = *p++;
    }
    xbuf[k++] = 13;
    pid = sys_fork(nbuf, xbuf, k);
    if (pid < 0) return -oserr;
    for (;;) {
        w = sys_wait();
        if (w == -1 && oserr) return -oserr;
        if (((unsigned)w >> 8) == (unsigned)pid) return w & 255;
    }
}

int o9_putstr(const char *s)
{
    while (*s) putchar(*s++);
    return 0;
}

int o9_puts(const char *s)
{
    o9_putstr(s);
    putchar(10);
    return 0;
}

u16 utoa_(u16 v, char *s, u8 base)
{
    char t[8];
    u8 n = 0, i;
    do { u8 d = (u8)(v % base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    for (i = 0; i < n; i++) s[i] = t[n - 1 - i];
    s[n] = 0;
    return n;
}

int atoi_(const char *s)
{
    int v = 0, neg = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') v = v * 10 + *s++ - '0';
    return neg ? -v : v;
}
