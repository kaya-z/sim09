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
 */
#ifndef MK_VERSION_H
#define MK_VERSION_H

#define MK09_VERSION    "1.3"
#define MX_VERSION      2           /* version of the MX executable format (docs/SPEC.md) */

#endif
