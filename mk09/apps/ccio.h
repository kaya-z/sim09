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
#define CC_BUF 128
#endif

#ifdef CC_HOST
typedef struct { FILE *f; } rfile;
typedef struct { FILE *f; u16 pos; } wfile;
static int r_open(rfile *r, const char *path) { r->f = fopen(path, "rb"); return r->f ? 0 : -1; }
static int r_getc(rfile *r) { return fgetc(r->f); }
static void r_close(rfile *r) { if (r->f) fclose(r->f); r->f = NULL; }
static int w_open(wfile *w, const char *path) { w->f = fopen(path, "wb+"); w->pos = 0; return w->f ? 0 : -1; }
static void w_putc(wfile *w, int c) { fputc(c, w->f); w->pos++; }
static void w_flush(wfile *w) { fflush(w->f); }
static void w_close(wfile *w) { if (w->f) fclose(w->f); w->f = NULL; }
static u16 w_tell(wfile *w) { return w->pos; }
static void w_patch(wfile *w, u16 at, const char *s, u16 n) { fseek(w->f, at, SEEK_SET); fwrite(s, 1, n, w->f); fseek(w->f, 0, SEEK_END); }
#else
typedef struct { int fd; u16 pos, cnt; u8 buf[CC_BUF]; } rfile;
typedef struct { int fd; u16 len; u16 pos; u8 buf[CC_BUF]; } wfile;
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
    w->pos++;
    if (w->len == CC_BUF) w_flush(w);
}
static void w_close(wfile *w) { if (w->fd >= 0) { w_flush(w); close(w->fd); } w->fd = -1; }
static u16 w_tell(wfile *w) { return w->pos; }
static void w_patch(wfile *w, u16 at, const char *s, u16 n)
{
    u16 rh, rl;
    w_flush(w);
    seek(w->fd, 0, at, 0, &rh, &rl);
    write(w->fd, s, n);
    seek(w->fd, 0, w->pos, 0, &rh, &rl);
}
#endif

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
