/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* id - the user and the group of this shell (2.0) */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    u8 u, g;
    int r = getuser(&u, &g);
    (void)argc; (void)argv;
    if (r < 0) { printf("id: %s\n", strerr_(r)); return 1; }
    printf("uid=%u gid=%u\n", (u16)u, (u16)g);
    return 0;
}
