/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* mem - the memory of the machine (64KB): the map, and the memory for the programs (the transient area, in pages of 256 bytes) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "version.h"
#include "memmap.h"
#define TOOL_VERSION "1.1"

static u16 tenths(unsigned long bytes) { return (u16)((bytes * 10UL + 512UL) / 1024UL); }   /* KB x 10 */

static void line(const char *what, u16 from, u16 to)    /* to: the last address */
{
    u16 t = tenths((unsigned long)to - from + 1UL);
    printf("  $%04x-$%04x  %3u.%u KB  %s\n", from, to, t / 10, t % 10, what);
}

int main(int argc, char **argv)
{
    u16 total, freep, big, procs, t;
#ifdef BOARD_FM11
    if (argc == 2 && !strcmp(argv[1], "-v")) { puts("mem " TOOL_VERSION MK09_FOR); return 0; }
#endif
    (void)argc; (void)argv;
    if (meminfo(&total, &freep, &big, &procs) < 0) { puts("mem: no answer"); return 1; }
#ifdef FM11_MMU
    {   /* Level 2: the machine has 128KB .. 1MB (the kernel has tried it at the start); MM hands out the pages that the system does not use, to the programs */
        u16 pp = total >> 4, pf = freep >> 4, sp;
        u8 g = 1, k;
        msg_t q;
        q.op = MM_MAP;
        q.a = 0;
        if (!mk_call(TID_MM, &q) && !q.op && q.c >= 1 && q.c <= 8) g = (u8)q.c;
        for (sp = 0, k = 0; k < 32; k++)                /* the pages of the first 128KB that MM does not manage */
            if (!((k < 16 ? MM_POOL0 : MM_POOL1) & (1u << (k & 15)))) sp++;
        sp -= 3;
        printf("physical memory (RAM %uKB = %u pages of 4KB):\n", (u16)g * 128, (u16)g * 32);
        printf("  system (kernel, servers, vectors)  %2u pages  %3u KB\n", sp, sp * 4);
        printf("  track buffer of format             %2u pages  %3u KB\n", 3, 12);
        printf("  programs (MM)                      %2u pages  %3u KB\n", pp, pp * 4);
        printf("    used                             %2u pages  %3u KB  (%u program%s)\n", pp - pf, (pp - pf) * 4, procs, procs == 1 ? "" : "s");
        printf("    free                             %2u pages  %3u KB\n", pf, pf * 4);
    }
    {   /* which page is what: K kernel, S servers, F format buffer, P a program, . free, - not used, H hardware (the I/O area is in the page 15) */
        msg_t m;
        u16 pool[2], fr[2], fw;
        u8 p, row, ch, wi, ng = 1;
        pool[0] = MM_POOL0;
        pool[1] = MM_POOL1;
        puts("  physical pages of 4KB (K kernel, S servers, F format, P programs, . free):");
        for (wi = 0; wi < 2 * ng; wi += 2) {
            m.op = MM_MAP;
            m.a = wi;
            fr[0] = fr[1] = 0;
            if (!mk_call(TID_MM, &m) && !m.op) { fr[0] = m.a; fr[1] = m.b; if (m.c >= 1 && m.c <= 8) ng = (u8)m.c; }
            p = wi * 16;                                /* (the first page of the line: 32 pages) */
            printf("    %03u-%03u  ", p, p + 31);
            for (fw = 0; fw < 32; fw++, p++) {
                row = fw >> 4;
                if (p >= 32) ch = (fr[row] & (1u << (fw & 15))) ? '.' : 'P';
                else if (pool[p >> 4] & (1u << (p & 15))) ch = (fr[row] & (1u << (fw & 15))) ? '.' : 'P';
                else if (p >= BOUNCE && p < BOUNCE + 3) ch = 'F';
                else if (p < 16) ch = (p <= ((K_DATA + K_DSZ - 1) >> 12)) ? 'K' : 'S';
                else ch = '-';
                putchar(ch);
            }
            putchar('\n');
        }
    }
    puts("logical map of the system (the servers see the first 64KB as it is):");
    line("vectors, boot buffer (the boot buffer is free)", 0x0000, SYS_BASE - 1);
    line("kernel (code, data, the ring of the console)", K_CODE, K_DATA + K_DSZ - 1);
    line("system code (TTY, DISK, FS)", K_DATA + K_DSZ, TPA_BASE - 1);
    line("system data, stacks, code of PM/MM (high)", DATA_BASE, 0xFBFF);
    puts("logical map of a program (every program has its own):");
    line("kernel (the same in every map)", 0x0000, 0x1FFF);
    line("program area (code, data, stack: up to 52KB)", 0x2000, 0xEFFF);
    return 0;
#elif defined(MMU_COCO)
    puts("logical map of the system (the servers see the memory as it is: the blocks of 8KB 0-7 are the physical blocks 0-7):");
    line("vectors, boot buffer (the boot buffer is free)", 0x0000, SYS_BASE - 1);
    line("kernel (code, data, the ring of the console)", K_CODE, K_DATA + K_DSZ - 1);
    line("system code (TTY, DISK, FS)", K_DATA + K_DSZ, TPA_BASE - 1);
    line("system data, stacks, code of PM/MM (high)", DATA_BASE, 0xFDFF);
    puts("logical map of a program (every program has its own):");
    line("kernel (the same in every map)", 0x0000, 0x1FFF);
    line("program area (code, data, stack: up to 56KB)", 0x2000, 0xFDFF);
    puts("                         ($FE00-$FFFF: the vectors and the I/O registers)");
#else
    puts("memory map (RAM 64KB):");
    line("vectors, boot buffer (the boot buffer is free)", 0x0000, SYS_BASE - 1);
    line("system code (kernel, TTY, FS)", SYS_BASE, TPA_BASE - 1);
    line("programs (transient area)", TPA_BASE, TPA_END - 1);
    line("I/O page", TPA_END, DATA_BASE - 1);
    line("system data, stacks, code of PM/MM (high)", DATA_BASE, 0xFEFF);
#endif
    puts("programs:");
    t = tenths((unsigned long)total << 8);
    printf("  total %u.%u KB (%u pages of 256 bytes)\n", t / 10, t % 10, total);
    t = tenths((unsigned long)(total - freep) << 8);
    printf("  used  %u.%u KB (%u pages), %u program%s running\n", t / 10, t % 10, total - freep, procs, procs == 1 ? "" : "s");
    t = tenths((unsigned long)freep << 8);
    printf("  free  %u.%u KB (%u pages), the biggest piece %u pages (%u bytes)\n", t / 10, t % 10, freep, big, (u16)(big << 8));
    return 0;
}
