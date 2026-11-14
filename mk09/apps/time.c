/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * time - run a command and show how long it took (like time of UNIX)
 *
 *     time command [argument...]
 *
 * The command is a program of /BIN (or a path), found as the shell does.  Its standard input and output are those of time (so  time cmd > file
 * redirects the command).  After the command has ended, the elapsed time is written to the console:
 *
 *     real    0m1.24s
 *
 * The elapsed (wall clock) time is counted in the ticks of the system (TICK_HZ, 50 per second, 20 ms), a 16 bit counter: up to 21 minutes (65536
 * ticks), a longer run shows the time modulo that.  The CPU time of a program is not known after it has ended.  The exit code of the command is
 * the exit code of time.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "mk09.h"
#include "version.h"
#define TOOL_VERSION "1.0"

extern u8 mk_in_h, mk_out_h;                    /* the handles of the redirected standard input / output (0xFF: the console) */

static char blk[ARGMAX];
static char path[MAXPATH];
static char out[40];

static int exists(const char *p)
{
    mkstat_t st;
    return stat(p, &st) == 0 && !(st.attr & FA_DIR);
}

static int find_program(const char *name)       /* the same search as the shell */
{
    if (strlen(name) > MAXPATH - 8) return 0;
    strcpy(path, name);
    if (strchr(name, '/') && exists(path)) return 1;
    strcat(path, ".MX");
    if (exists(path) || strchr(name, '/')) return exists(path);
    strcpy(path, "/BIN/");
    strcat(path, name);
    strcat(path, ".MX");
    return exists(path);
}

static char *put_u(char *p, u16 v, u8 width)    /* decimal, at least width digits */
{
    char t[6];
    u8 n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n < width) t[n++] = '0';
    while (n) *p++ = t[--n];
    return p;
}

int main(int argc, char **argv)
{
    u16 t0, ticks, sec, hund, in_h, out_h;
    u8 i, l;
    u16 len;
    char *p;
    int r;

    if (argc == 2 && !strcmp(argv[1], "-v")) { puts("time " TOOL_VERSION MK09_FOR); return 0; }
    if (argc < 2) { puts("usage: time command [argument...]"); return 1; }
    if (!find_program(argv[1])) {
        printf("time: %s: command not found\n", argv[1]);
        return 127;
    }
    strcpy(blk, path);
    len = (u16)strlen(path) + 1;
    for (i = 2; i < argc; i++) {
        l = (u8)strlen(argv[i]) + 1;
        if (len + l + 1 > ARGMAX - SB_ARGS) { puts("time: arguments too long"); return 1; }
        memcpy(blk + len, argv[i], l);
        len += l;
    }
    blk[len++] = 0;
    in_h = out_h = NOHANDLE;                    /* (if/else: CMOC does not like ?: with different types) */
    if (mk_in_h != 0xFF) in_h = mk_in_h;
    if (mk_out_h != 0xFF) out_h = mk_out_h;

    t0 = mk_ticks();
    r = spawn(blk, len, in_h, out_h);
    ticks = (u16)(mk_ticks() - t0);
    if (r < 0) { printf("time: %s: %s\n", argv[1], strerr_(r)); return 126; }

    sec = ticks / TICK_HZ;                      /* the elapsed time: minutes, seconds and hundredths */
    hund = (u16)((ticks % TICK_HZ) * (100 / TICK_HZ));
    p = out;
    *p++ = '\n';
    memcpy(p, "real\t", 5); p += 5;
    p = put_u(p, sec / 60, 1);
    *p++ = 'm';
    p = put_u(p, sec % 60, 1);
    *p++ = '.';
    p = put_u(p, hund, 2);
    *p++ = 's';
    *p++ = '\n';
    write(2, out, (u16)(p - out));              /* the console (the standard output may be a file) */
    return r;
}
