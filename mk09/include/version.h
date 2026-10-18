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
 */
#ifndef MK_VERSION_H
#define MK_VERSION_H

#define MK09_VERSION    "1.1"
#define MX_VERSION      2           /* version of the MX executable format (docs/SPEC.md) */

#endif
