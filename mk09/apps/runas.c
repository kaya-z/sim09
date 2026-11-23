/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* runas - run a program as another user (root only): runas UID GID program [args ...]   (a tool for the tests of 2.0; login and su come later) */
#include "types.h"
#include "mklib.h"
#include "pathnames.h"

int main(int argc, char **argv)
{
    char blk[ARGMAX];
    u16 len;
    int i, r;
    if (argc < 4) { puts("usage: runas uid gid program [args ...]"); return 1; }
    r = setuser((u8)atoi_(argv[1]), (u8)atoi_(argv[2]));
    if (r < 0) { printf("runas: %s\n", strerr_(r)); return 1; }
    strcpy(blk, PN_BIN);
    strcat(blk, argv[3]);
    strcat(blk, PN_EXT_MX);
    len = (u16)strlen(blk) + 1;
    for (i = 4; i < argc; i++) {
        u16 l = (u16)strlen(argv[i]) + 1;
        if (len + l + 1 > ARGMAX - SB_ARGS) { puts("runas: the command line is too long"); return 1; }
        memcpy(blk + len, argv[i], l);
        len += l;
    }
    blk[len++] = 0;
    r = exec(blk, len);
    printf("runas: %s: %s\n", argv[3], strerr_(r > 0 ? -r : r));
    return 1;
}
