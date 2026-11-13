/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* mem - the memory of the machine (64KB): the map, and the memory for the programs (the transient area, in pages of 256 bytes) */
#include "types.h"
#include "mklib.h"
#include "memmap.h"
#define TOOL_VERSION "1.1"

static u16 tenths(unsigned long bytes) { return (u16)((bytes * 10UL + 512UL) / 1024UL); }   /* KB x 10 */

static void line(const char *what, u16 from, u16 to)    /* to: the last address */
{
    u16 t = tenths((unsigned long)to - from + 1UL);
    printf("  $%04x-$%04x  %3u.%u KB  %s\n", from, to, t / 10, t % 10, what);
}

int main(void)
{
    u16 total, freep, big, procs, t;
    if (meminfo(&total, &freep, &big, &procs) < 0) { puts("mem: no answer"); return 1; }
    puts("memory map (RAM 64KB):");
    line("vectors, boot buffer (the boot buffer is free)", 0x0000, SYS_BASE - 1);
    line("system code (kernel, TTY, FS)", SYS_BASE, TPA_BASE - 1);
    line("programs (transient area)", TPA_BASE, TPA_END - 1);
    line("I/O page", TPA_END, DATA_BASE - 1);
    line("system data, stacks, code of PM/MM (high)", DATA_BASE, 0xFEFF);
    puts("programs:");
    t = tenths((unsigned long)total << 8);
    printf("  total %u.%u KB (%u pages of 256 bytes)\n", t / 10, t % 10, total);
    t = tenths((unsigned long)(total - freep) << 8);
    printf("  used  %u.%u KB (%u pages), %u program%s running\n", t / 10, t % 10, total - freep, procs, procs == 1 ? "" : "s");
    t = tenths((unsigned long)freep << 8);
    printf("  free  %u.%u KB (%u pages), the biggest piece %u pages (%u bytes)\n", t / 10, t % 10, freep, big, (u16)(big << 8));
    return 0;
}
