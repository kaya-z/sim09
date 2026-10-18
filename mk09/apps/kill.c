/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* kill - stop a program that runs in the background:  kill TID  (the task ids are the first column of ps) */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int r, i;
    if (argc < 2) { puts("usage: kill tid..."); return 1; }
    for (i = 1; i < argc; i++) {
        r = kill((u8)atoi_(argv[i]));
        if (r < 0) { printf("kill: %s: %s\n", argv[i], strerr_(r)); return 1; }
    }
    return 0;
}
