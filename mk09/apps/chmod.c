/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* chmod - change the permissions: chmod MODE file ...   (MODE is octal: 755, 644, 4755 = setuid; the owner or root) */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    u16 mode = 0;
    char *p;
    if (argc < 3) { puts("usage: chmod mode file ... (octal, e.g. 755)"); return 1; }
    for (p = argv[1]; *p; p++) {
        if (*p < '0' || *p > '7') { puts("chmod: the mode is an octal number"); return 1; }
        mode = (u16)((mode << 3) | (*p - '0'));
    }
    mode = (u16)((mode & 0x1FF) | ((mode & 04000) ? 01000 : 0) | (mode & 02000));      /* (4755: the setuid bit, as on Unix; 2755: the access to the disk, root only) */
    for (i = 2; i < argc; i++) {
        r = chmod(argv[i], mode);
        if (r < 0) { printf("chmod: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
