/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * version.h - the version of MK09 (shown at boot and by `ver`; the documents carry the same number)
 *
 * Raise it with every change of the design or of the sources (docs/DESIGN.md, "改版履歴").
 *
 *   1.0  2026-10-15  first version (kernel, TTY / DISK / FS / PM, shell, jwp)
 *   1.1  2026-10-18  relocatable programs (MX version 2), memory manager (servers/mm.c), several
 *                    programs at the same time (`prog &`, kill, mem), console owner (TTYC_SETFG)
 *   1.2  2026-10-25  the memory manager is a task of its own (servers/mm.s, TID_MM = 5, protocol MM_ALLOC / MM_FREE / MM_INFO;
 *                    only PM may allocate and free); the tasks of the programs start at 6, NTASK 11.  (Backported from the
 *                    YSD9000 port, MK09 1.2.  The version is the one of the whole system, not of a server.)
 *   1.3  2026-10-25  the start-up message lists MM.  (YSD9000 1.3 also gave mk_tinfo the figures of the unused stack:
 *                    here the memory is flat and `stk` reads it itself, so that stays as it is.)
 *   1.4  2026-10-27  faster message round trip (3.1 ms -> 2.2 ms): the scheduler picks within the classes of the task numbers
 *                    (servers, programs, idle) instead of searching all the tasks for the best priority, a message is copied
 *                    by an unrolled loop, a receive does not look for blocked senders when there are none, the idle task is
 *                    made from the boot table.
 *   1.5  2026-10-27  the portable C kernel gets the same speed-ups in a form that does not depend on the CPU (a one-pass pick that
 *                    still looks at the priority values, arch_copymsg() that an architecture may replace, the send hint):
 *                    round trip 6.7 ms -> 5.0 ms.
 *   1.6  2026-10-27  from the YSD9000 port (its MK09 up to 1.13): pipes in the shell (`a | b | c` through /PIPE0.TMP and /PIPE1.TMP),
 *                    `more` reads its standard input, programs get up to 16 arguments, the shell says when the kernel stopped a program;
 *                    BASIC 1.1 (the name and version in the banner).  Not taken over: the development tools (as, ld, ar, cc, Pascal: they
 *                    make YSD9000 code), the timer fast path (the console server must poll the UART every tick here), the 8-block cache
 *                    of FS (no room in the 64 KB).
 */
#ifndef MK_VERSION_H
#define MK_VERSION_H

#define MK09_VERSION    "1.6"
#define MX_VERSION      2           /* version of the MX executable format (docs/SPEC.md) */

#endif
