/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * mklib.h of the OS-9 build of the compiler tools (os9/pascal): the part of the C library of MK09 (mk09/include/mklib.h) that pas0 pas1 cg as pc use,
 * written over the system calls of OS-9 (os9shim.c).  The tools are the same source files as on MK09 (mk09/apps), compiled by CMOC with --os9.
 *
 * Differences from MK09: a file name is an OS-9 path (a name that starts with /LIB/ is looked for in /d0/LIB/);  exec chains to the next program
 * with F$Chain (the program name is the first string of the block: "/BIN/NAME.MX" is NAME);  the tools run on Level 2 only (they need a 64K address space).
 */
#ifndef MKLIB_H
#define MKLIB_H

#include <cmoc.h>
#include "types.h"

#define FO_READ     0x01
#define FO_WRITE    0x02
#define FO_CREATE   0x04
#define FO_TRUNC    0x08
#define FO_APPEND   0x10
#define FO_EXEC     0x80        /* with FO_CREATE: the file is a program (the attributes: read, write, execute, public read, public execute) */
#define ARGMAX      200
#define SB_ARGS     4

int  o9_open(const char *path, u8 flags);       /* the path number (>= 0), or -(the error of OS-9) */
int  o9_close(int fd);
int  o9_read(int fd, void *buf, u16 n);         /* the number of bytes (0 at the end of the file), or -error */
int  o9_write(int fd, const void *buf, u16 n);
int  o9_seek(int fd, u16 hi, u16 lo, u8 whence, u16 *rhi, u16 *rlo);     /* whence 0 only */
int  o9_unlink(const char *path);
int  o9_exec(const char *block, u16 len);        /* "prog\0arg\0...\0\0": chain to prog with the arguments; returns only when it fails */
int  o9_run(int argc, char **argv);              /* run argv[0] with the arguments argv[1..] and wait: its exit status, or -error */
int  o9_putstr(const char *s);
int  o9_puts(const char *s);                    /* with a line end */
u16  utoa_(u16 v, char *s, u8 base);
int  atoi_(const char *s);

#define open   o9_open
#define close  o9_close
#define read   o9_read
#define write  o9_write
#define seek   o9_seek
#define unlink o9_unlink
#define exec   o9_exec
#define putstr o9_putstr
#define puts   o9_puts

#endif
