/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* passwd - change a password: passwd [user]   (a user changes the own password after the old one; root changes any).  This file has the setuid bit (owner root). */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"

#define TOOL_VERSION "1.1"

int main(int argc, char **argv)
{
    char p1[40], p2[40], me[PW_NAME];
    const char *who;
    pw_t u;
    u8 ru = 0xFF;
    int r;

    getruser(&ru);
    if (argc > 1) who = argv[1];
    else who = unamestr(ru, me);
    if (getpwnam(who, &u) < 0) { printf("passwd: no user %s\n", who); return 1; }
    if (ru != 0 && ru != u.uid) { puts("passwd: not allowed"); return 1; }
    if (ru != 0) {
        getpass("Old password: ", p1, sizeof(p1));
        if (pwcheck(who, p1) < 0) { puts("passwd: wrong password"); mk_sleep(100); return 1; }
    }
    getpass("New password: ", p1, sizeof(p1));
    getpass("Again: ", p2, sizeof(p2));
    if (strcmp(p1, p2)) { puts("passwd: the passwords differ"); return 1; }
    if (!p1[0] || strlen(p1) > 32) { puts("passwd: the password has 1 to 32 characters"); return 1; }
    r = pwset(who, p1);
    memset(p1, 0, sizeof(p1));
    memset(p2, 0, sizeof(p2));
    if (r < 0) { printf("passwd: %s\n", strerr_(r)); return 1; }
    puts("password changed");
    return 0;
}
