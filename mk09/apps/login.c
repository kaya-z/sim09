/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* login - ask for a user name and a password, then start the shell of the user (2.0).  PM starts it as root at the start and whenever a session ends. */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"
#include "pathnames.h"

#define TOOL_VERSION "1.1"

int main(int argc, char **argv)
{
    char name[PW_NAME + 8], pw[40], blk[ARGMAX], *nm;
    pw_t u;
    u16 len;
    int r;
    (void)argc; (void)argv;
    for (;;) {
        putstr("\nMK09 login: ");
        fflush();
        if (gets_(name, sizeof(name)) <= 0) continue;
        for (nm = name; *nm == ' '; nm++) ;                             /* (blanks in front and behind are not part of the name) */
        len = (u16)strlen(nm);
        while (len && nm[len - 1] == ' ') nm[--len] = 0;
        if (!*nm) continue;
        if (getpass("Password: ", pw, sizeof(pw)) < 0) continue;
        r = pwcheck(nm, pw);
        memset(pw, 0, sizeof(pw));
        if (r == 0 && getpwnam(nm, &u) == 0) {
            if (pwexpired(nm)) {                                        /* the first password (the one of the image): it must be changed now */
                char n1[40], n2[40];
                u8 tries = 0, done = 0;
                puts("\nThe password must be changed.");
                while (tries++ < 3 && !done) {
                    getpass("New password: ", n1, sizeof(n1));
                    getpass("Again: ", n2, sizeof(n2));
                    if (strcmp(n1, n2)) puts("The passwords differ.");
                    else if (!n1[0] || !strcmp(n1, nm)) puts("Choose a password that is not empty and not the name.");
                    else if (pwset(nm, n1) < 0) puts("login: cannot write the password.");
                    else done = 1;
                }
                memset(n1, 0, sizeof(n1));
                memset(n2, 0, sizeof(n2));
                if (!done) continue;
                puts("password changed");
            }
            if (setuser(u.uid, u.gid) < 0) { puts("login: cannot change the user"); continue; }
            if (chdir(u.home) < 0) chdir("/");
            strcpy(blk, u.shell);
            len = (u16)strlen(blk) + 1;
            blk[len++] = 0;
            r = exec(blk, len);
            printf("login: %s: %s\n", u.shell, strerr_(r > 0 ? -r : r));
            continue;
        }
        puts("Login incorrect");
        mk_sleep(100);
    }
    return 0;
}
