/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* echo - print the arguments */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        putstr(argv[i]);
        putchar(i + 1 < argc ? ' ' : '\n');
    }
    if (argc == 1) putchar('\n');
    return 0;
}
