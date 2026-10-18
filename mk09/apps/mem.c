/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* mem - the memory for the programs (the transient area, in pages of 256 bytes) and the programs that run */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u16 total, freep, big, procs;
    if (meminfo(&total, &freep, &big, &procs) < 0) { puts("mem: no answer"); return 1; }
    printf("%u pages of 256 bytes: %u free (%u bytes), the biggest piece %u pages (%u bytes); %u programs\n",
           total, freep, (u16)(freep << 8), big, (u16)(big << 8), procs);
    return 0;
}
