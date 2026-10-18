/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* spin - loop until it is stopped with ^C (a test of the foreground program handling of PM) */
#include "types.h"
#include "mklib.h"

int main(void)
{
    u16 n = 0;
    puts("spinning (^C stops)");
    for (;;)
        n++;
    return n;
}
