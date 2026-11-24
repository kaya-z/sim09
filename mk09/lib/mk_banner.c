/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_banner.c - part of the MK09 C library (2.0): the logo and the version of the system.  login shows it before the prompt (in 1.x the shell does, apps/sh.c). */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "version.h"

#ifdef MK09_V2
void banner(void)
{
    puts("\n"
         " __  __ _  __  ___   ___\n"
         "|  \\/  | |/ / / _ \\ / _ \\\n"
         "| |\\/| | ' / | | | | (_) |\n"
         "| |  | | . \\ | |_| |\\__, |\n"
         "|_|  |_|_|\\_\\ \\___/   /_/\n"
         );
    putstr(" MC6809 microkernel " MK09_VERSION MK09_FOR " (kernel " MK09_KERNEL_VERSION ")  --  " MK09_MEM);
#ifdef FM11_MMU
    {   /* Level 2: the RAM that the kernel found at the start (MM tells the steps of 128KB) */
        msg_t q;
        q.op = MM_MAP;
        q.a = 0;
        if (!mk_call(TID_MM, &q) && !q.op && q.c >= 1 && q.c <= 8) printf(", RAM %uKB", (u16)q.c * 128);
    }
#endif
    putstr("\n");
    puts(" TTY + DISK + FS(FAT16) + PM  /  type help\n"
         " (c) 2026 YUKARI Semiconductor Devices\n");
}
#endif
