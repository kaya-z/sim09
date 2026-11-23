/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* chown - change the owner and the group (root only): chown UID[:GID] file ...  (numbers; the names come with /etc/passwd) */
#include "types.h"
#include "mklib.h"

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    u16 uid, gid = 0xFFFF;
    char *p = argv[1];
    if (argc < 3) { puts("usage: chown uid[:gid] file ..."); return 1; }
    uid = (u16)atoi_(p);
    while (*p && *p != ':') p++;
    if (*p == ':') gid = (u16)atoi_(p + 1);
    for (i = 2; i < argc; i++) {
        r = chown(argv[i], uid, gid);
        if (r < 0) { printf("chown: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
