/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* passwd - change a password: passwd [user]   (a user changes the own password after the old one; root changes any).  This file has the setuid bit (owner root). */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "pwd.h"

#define TOOL_VERSION "1.0"

static char old[768], nw[800];

int main(int argc, char **argv)
{
    char p1[40], p2[40], salt[9], hex[65], me[PW_NAME];
    const char *who;
    pw_t u;
    u8 ru = 0xFF;
    u16 t, d, i;
    int fd, n, r;
    char *p, *e;
    u16 len = 0, l;

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
    gettime(&d, &t);                                            /* the salt: 8 hexadecimal digits from the clock and the uptime */
    t = (u16)(t ^ (mk_ticks() * 31));
    for (i = 0; i < 4; i++) { salt[i] = "0123456789abcdef"[(t >> (12 - 4 * i)) & 15]; salt[4 + i] = "0123456789abcdef"[((d ^ (t << 3)) >> (12 - 4 * i)) & 15]; }
    salt[8] = 0;
    pwhash(p1, salt, PW_COUNT, hex);
    memset(p1, 0, sizeof(p1));
    memset(p2, 0, sizeof(p2));
    fd = open(F_SHADOW, FO_READ);
    if (fd < 0) { printf("passwd: %s: %s\n", F_SHADOW, strerr_(fd)); return 1; }
    n = read(fd, old, sizeof(old) - 1);
    close(fd);
    if (n < 0) n = 0;
    old[n] = 0;
    l = (u16)strlen(who);
    for (p = old; *p; p = e) {                                  /* the lines of the others as they are, the line of the user new */
        e = p;
        while (*e && *e != '\n') e++;
        if (*e) e++;
        if (!strncmp(p, who, l) && p[l] == ':') continue;
        memcpy(nw + len, p, (u16)(e - p));
        len += (u16)(e - p);
    }
    memcpy(nw + len, who, l); len += l;
    nw[len++] = ':';
    memcpy(nw + len, salt, 8); len += 8;
    nw[len++] = ':';
    memcpy(nw + len, hex, 64); len += 64;
    nw[len++] = '\n';
    unlink("/etc/shadow.new");
    fd = open("/etc/shadow.new", FO_WRITE | FO_CREATE | FO_TRUNC);
    if (fd < 0) { printf("passwd: %s\n", strerr_(fd)); return 1; }
    r = write(fd, nw, len);
    close(fd);
    if (r != (int)len) { puts("passwd: cannot write the file"); unlink("/etc/shadow.new"); return 1; }
    chmod("/etc/shadow.new", 0600);
    chown("/etc/shadow.new", 0, 0);
    unlink(F_SHADOW);
    r = rename("/etc/shadow.new", F_SHADOW);
    if (r < 0) { printf("passwd: %s\n", strerr_(r)); return 1; }
    puts("password changed");
    return 0;
}
