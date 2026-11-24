/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* login - ask for a user name and a password, then start the shell of the user (2.0).  PM starts it as root at the start and whenever a session ends. */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"
#include "pathnames.h"
#ifdef MK_MMU
#include "mk09.h"
#endif

#if defined(MMU_COCO) && defined(MK09_V2)
#define RAMDISK 1                   /* the sbc09 with the MMU: the RAM disk (drive 1) is formatted and mounted by the first login (the terminal 0), see cc */
#define TOOL_VERSION "1.2"
#else
#define TOOL_VERSION "1.1"
#endif

int main(int argc, char **argv)
{
    char name[PW_NAME + 8], pw[40], blk[ARGMAX], *nm;
    pw_t u;
    u16 len;
    int r;
    (void)argc; (void)argv;
    banner();                                                           /* the logo and the version, as the shell showed them in 1.x (before the first prompt) */
#ifdef RAMDISK
    {
        u8 line = 0, uid, run;
        if (taskuser(mk_self(), &line, &uid, &run) == 0 && line == 0 && fsmounted(1) != 1) {   /* (after a reset the RAM is empty: a new file system) */
            strcpy(blk, "/bin/format.mx");                          /* (the block: the program and its arguments, each ended by 0, and one more 0) */
            len = (u16)strlen(blk) + 1;
            strcpy(blk + len, "-y"); len += 3;
            strcpy(blk + len, "1"); len += 2;
            strcpy(blk + len, "RAM"); len += 4;
            blk[len++] = 0;
            if (spawn(blk, len, NOHANDLE, NOHANDLE) == 0) fsmount(1);
        }
    }
#endif
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
