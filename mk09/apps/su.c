/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* su - start a shell as another user: su [user]   (the user is root by default; root does not need the password).  This file has the setuid bit (owner root). */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"
#include "pathnames.h"

#define TOOL_VERSION "1.0"

int main(int argc, char **argv)
{
    char pw[40], blk[ARGMAX];
    const char *who = argc > 1 ? argv[1] : "root";
    pw_t u;
    u8 ru = 0xFF;
    u16 len;
    int r;
    if (getpwnam(who, &u) < 0) { printf("su: no user %s\n", who); return 1; }
    getruser(&ru);
    if (ru != 0) {                                              /* (the real user: this program runs as root) */
        getpass("Password: ", pw, sizeof(pw));
        r = pwcheck(who, pw);
        memset(pw, 0, sizeof(pw));
        if (r < 0) { puts("su: wrong password"); mk_sleep(100); return 1; }
    }
    if (setuser(u.uid, u.gid) < 0) { puts("su: not allowed (is this file setuid root?)"); return 1; }
    strcpy(blk, u.shell);
    len = (u16)strlen(blk) + 1;
    strcpy(blk + len, "-r");
    len += 3;
    blk[len++] = 0;
    r = exec(blk, len);
    printf("su: %s: %s\n", u.shell, strerr_(r > 0 ? -r : r));
    return 1;
}
