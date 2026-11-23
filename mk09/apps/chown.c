/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* chown - change the owner and the group (root only): chown USER[:GROUP] file ...  (names or numbers) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"

static int number(const char *s) { return *s >= '0' && *s <= '9'; }

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    u16 uid = 0xFFFF, gid = 0xFFFF;
    char *p, *g;
    pw_t u;
    u8 x;
    if (argc < 3) { puts("usage: chown user[:group] file ..."); return 1; }
    p = argv[1];
    g = strchr(p, ':');
    if (g) *g++ = 0;
    if (*p) {
        if (number(p)) uid = (u16)atoi_(p);
        else if (getpwnam(p, &u) == 0) uid = u.uid;
        else { printf("chown: no user %s\n", p); return 1; }
    }
    if (g && *g) {
        if (number(g)) gid = (u16)atoi_(g);
        else if (getgrnam(g, &x) == 0) gid = x;
        else { printf("chown: no group %s\n", g); return 1; }
    }
    for (i = 2; i < argc; i++) {
        r = chown(argv[i], uid, gid);
        if (r < 0) { printf("chown: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
