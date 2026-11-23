/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* chgrp - change the group (root only): chgrp GROUP file ...  (a name or a number) */
#include "types.h"
#include "mklib.h"
#include "pwd.h"

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    u16 gid;
    u8 x;
    if (argc < 3) { puts("usage: chgrp group file ..."); return 1; }
    if (argv[1][0] >= '0' && argv[1][0] <= '9') gid = (u16)atoi_(argv[1]);
    else if (getgrnam(argv[1], &x) == 0) gid = x;
    else { printf("chgrp: no group %s\n", argv[1]); return 1; }
    for (i = 2; i < argc; i++) {
        r = chown(argv[i], 0xFFFF, gid);
        if (r < 0) { printf("chgrp: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
