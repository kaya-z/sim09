/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * ccio.h - buffered files for the programs of the C compiler (cpp, cc1, cg, cc): the same source on MK09 (mklib.h) and on a PC (-DCC_HOST)
 *
 *   rfile: r_open r_getc r_close          wfile: w_open w_putc w_puts w_num w_flush w_close w_tell w_patch
 * r_getc returns the next byte, or -1 at the end of the file.  w_tell is the number of bytes written so far; w_patch overwrites bytes
 * that were written before (the output is flushed first).  A failure of the host library or of the file system stops the program.
 */
#ifndef CCIO_H
#define CCIO_H

#ifdef CC_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int8_t s8;
#define NULL ((void *)0)
#define CC_BUF 512
#else
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#ifndef CC_BUF
#define CC_BUF 64            /* (cg and pas0 have room for a bigger one: -DCC_BUF=256, fewer reads) */
#endif
#endif

#ifdef CC_HOST
typedef struct { FILE *f; } rfile;
typedef struct { FILE *f; u16 pos, ph; } wfile;
static int r_open(rfile *r, const char *path) { r->f = fopen(path, "rb"); return r->f ? 0 : -1; }
static int r_getc(rfile *r) { return fgetc(r->f); }
static void r_close(rfile *r) { if (r->f) fclose(r->f); r->f = NULL; }
static void r_seek(rfile *r, u16 hi, u16 lo) { fseek(r->f, ((long)hi << 16) | lo, SEEK_SET); }
static void r_skip(rfile *r, u16 n) { fseek(r->f, n, SEEK_CUR); }
static int w_open(wfile *w, const char *path) { w->f = fopen(path, "wb+"); w->pos = 0; w->ph = 0; return w->f ? 0 : -1; }
static void w_putc(wfile *w, int c) { fputc(c, w->f); if (++w->pos == 0) w->ph++; }
static void w_flush(wfile *w) { fflush(w->f); }
static void w_close(wfile *w) { if (w->f) fclose(w->f); w->f = NULL; }
static u16 w_tell(wfile *w) { return w->pos; }
static u16 w_tellh(wfile *w) { return w->ph; }
static void w_patch(wfile *w, u16 ah, u16 at, const char *s, u16 n) { fseek(w->f, ((long)ah << 16) | at, SEEK_SET); fwrite(s, 1, n, w->f); fseek(w->f, 0, SEEK_END); }
#else
typedef struct { int fd; u16 pos, cnt; u8 buf[CC_BUF]; } rfile;
typedef struct { int fd; u16 len; u16 pos, ph; u8 buf[CC_BUF]; } wfile;
static int r_open(rfile *r, const char *path)
{
    r->fd = open(path, FO_READ);
    r->pos = r->cnt = 0;
    return r->fd < 0 ? -1 : 0;
}
static int r_getc(rfile *r)
{
    if (r->pos == r->cnt) {
        int n = read(r->fd, r->buf, CC_BUF);
        if (n <= 0) return -1;
        r->pos = 0;
        r->cnt = (u16)n;
    }
    return r->buf[r->pos++];
}
static void r_close(rfile *r) { if (r->fd >= 0) close(r->fd); r->fd = -1; }
static void r_seek(rfile *r, u16 hi, u16 lo) { u16 rh, rl; seek(r->fd, hi, lo, 0, &rh, &rl); r->pos = r->cnt = 0; }
/* skip n bytes: inside the buffer it is only a change of the position */
static void r_skip(rfile *r, u16 n)
{
    while (n) {
        u16 a = (u16)(r->cnt - r->pos);
        if (!a) { if (r_getc(r) < 0) return; n--; continue; }
        if (a > n) a = n;
        r->pos = (u16)(r->pos + a);
        n = (u16)(n - a);
    }
}
static int w_open(wfile *w, const char *path)
{
    w->fd = open(path, FO_WRITE | FO_CREATE | FO_TRUNC | FO_READ);
    w->len = 0;
    w->pos = 0;
    return w->fd < 0 ? -1 : 0;
}
static void w_flush(wfile *w)
{
    if (w->len) { write(w->fd, w->buf, w->len); w->len = 0; }
}
static void w_putc(wfile *w, int c)
{
    w->buf[w->len++] = (u8)c;
    if (++w->pos == 0) w->ph++;
    if (w->len == CC_BUF) w_flush(w);
}
static void w_close(wfile *w) { if (w->fd >= 0) { w_flush(w); close(w->fd); } w->fd = -1; }
static u16 w_tell(wfile *w) { return w->pos; }
static u16 w_tellh(wfile *w) { return w->ph; }
static void w_patch(wfile *w, u16 ah, u16 at, const char *s, u16 n)
{
    u16 rh, rl;
    w_flush(w);
    seek(w->fd, ah, at, 0, &rh, &rl);
    write(w->fd, s, n);
    seek(w->fd, w->ph, w->pos, 0, &rh, &rl);
}
#endif

/* The stages of the compiler follow each other without a program that waits for them: the command line of a stage can end with
 *   ; next-program arguments ; ...
 * and when the stage has done its work it runs the next program in its place (exec: the memory of the stage is given back first). */
static int tail_start(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) if (argv[i][0] == ';' && argv[i][1] == 0) return i;
    return argc;
}

static int run_tail(int argc, char **argv, int at)
{
    if (at + 1 >= argc) return 0;
#ifdef CC_HOST
    {
        char cmd[600];
        const char *bin = getenv("CC_BIN");
        int i;
        cmd[0] = 0;
        if (bin) { strcat(cmd, bin); strcat(cmd, "/"); }
        for (i = at + 1; i < argc; i++) {
            if (i > at + 1) strcat(cmd, " ");
            if (argv[i][0] == ';' && argv[i][1] == 0) strcat(cmd, "\\;"); else strcat(cmd, argv[i]);
        }
        return system(cmd);
    }
#else
    {
        char blk[ARGMAX];
        u16 len;
        int i;
        strcpy(blk, "/BIN/");
        strcat(blk, argv[at + 1]);
        strcat(blk, ".MX");
        len = (u16)strlen(blk) + 1;
        for (i = at + 2; i < argc; i++) {
            u16 l = (u16)strlen(argv[i]) + 1;
            if (len + l + 1 > ARGMAX - SB_ARGS) { puts("cc: the command line of the next stage is too long"); return 1; }
            memcpy(blk + len, argv[i], l);
            len += l;
        }
        blk[len++] = 0;
        return exec(blk, len);
    }
#endif
}

static void w_puts(wfile *w, const char *s) { while (*s) w_putc(w, *s++); }
/* a signed decimal number */
static void w_num(wfile *w, int v)
{
    char t[7];
    u8 n = 0;
    u16 u;
    if (v < 0) { w_putc(w, '-'); u = (u16)(-v); } else u = (u16)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) w_putc(w, t[--n]);
}

#endif
