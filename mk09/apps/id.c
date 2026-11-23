/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* id - the user and the group of this shell (2.0) */
#include "types.h"
#include "mklib.h"
#include "pwd.h"

int main(int argc, char **argv)
{
    u8 u, g;
    char nu[PW_NAME], ng[PW_NAME];
    int r = getuser(&u, &g);
    (void)argc; (void)argv;
    if (r < 0) { printf("id: %s\n", strerr_(r)); return 1; }
    printf("uid=%u(%s) gid=%u(%s)\n", (u16)u, unamestr(u, nu), (u16)g, gnamestr(g, ng));
    return 0;
}
