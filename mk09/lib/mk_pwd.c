/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* lib/mk_pwd.c - part of the MK09 C library (2.0): the user database (include/pwd.h) */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#ifdef MK09_V2
#include "pwd.h"

int mk_rpc(u8 dst, msg_t *m, u8 gfl);
static char fbuf[768];                  /* the file that is looked at (a line is split at the colons) */

/* look for the line whose field 'key' is 'val' in a file of lines with colons; f[] gets the fields (in fbuf); returns the number of fields, or a negative error */
static int scan(const char *file, u8 key, const char *val, char **f, u8 maxf)
{
    int fd = open(file, FO_READ), n;
    char *p, *e;
    u8 k;
    if (fd < 0) return fd;
    n = read(fd, fbuf, sizeof(fbuf) - 1);
    close(fd);
    if (n < 0) return n;
    fbuf[n] = 0;
    for (p = fbuf; *p;) {
        e = p;
        while (*e && *e != '\n') e++;
        if (*e) *e++ = 0;
        k = 0;
        f[k++] = p;
        for (; *p; p++)
            if (*p == ':') { *p = 0; if (k < maxf) f[k++] = p + 1; }
        if (key < k && !strcmp(f[key], val)) return k;
        p = e;
    }
    return -E_NOENT;
}

static void cp(char *d, const char *s, u8 n)
{
    u8 i;
    for (i = 0; i + 1 < n && s[i]; i++) d[i] = s[i];
    d[i] = 0;
}

static int pwfill(int k, char **f, pw_t *pw)
{
    if (k < 5) return -E_FORMAT;
    cp(pw->name, f[0], PW_NAME);
    pw->uid = (u8)atoi_(f[1]);
    pw->gid = (u8)atoi_(f[2]);
    cp(pw->home, f[3], sizeof(pw->home));
    cp(pw->shell, f[4], sizeof(pw->shell));
    return 0;
}

int getpwnam(const char *name, pw_t *pw)
{
    char *f[5];
    int k = scan(F_PASSWD, 0, name, f, 5);
    return k < 0 ? k : pwfill(k, f, pw);
}

int getpwuid(u8 uid, pw_t *pw)
{
    char *f[5], n[6];
    int k;
    utoa_(uid, n, 10);
    k = scan(F_PASSWD, 1, n, f, 5);
    return k < 0 ? k : pwfill(k, f, pw);
}

int getgrnam(const char *name, u8 *gid)
{
    char *f[2];
    int k = scan(F_GROUP, 0, name, f, 2);
    if (k < 0) return k;
    if (k < 2) return -E_FORMAT;
    *gid = (u8)atoi_(f[1]);
    return 0;
}

int getgrgid(u8 gid, char *name)
{
    char *f[2], n[6];
    int k;
    utoa_(gid, n, 10);
    k = scan(F_GROUP, 1, n, f, 2);
    if (k < 0) return k;
    cp(name, f[0], PW_NAME);
    return 0;
}

int getshadow(const char *name, char *salt, char *hash)
{
    char *f[4];
    int k = scan(F_SHADOW, 0, name, f, 4);
    if (k < 0) return k;
    if (k < 3) return -E_FORMAT;
    cp(salt, f[1], 9);
    cp(hash, f[2], 65);
    return 0;
}

int pwexpired(const char *name)
{
    char *f[4];
    int k = scan(F_SHADOW, 0, name, f, 4);
    return k >= 4 && f[3][0] == '1';
}

static char sold[768], snew[800];

/* a new password for a user: a salt from the clock and the uptime, the line of the user in /etc/shadow is made again (without the mark), the file is written as a new
 * file and takes the place of the old one (FAT has no atomic rewrite) */
int pwset(const char *name, const char *pw)
{
    char salt[9], hex[65];
    u16 t, d, i, l = (u16)strlen(name), len = 0;
    int fd, n, r;
    char *p, *e;
    if (!pw[0] || strlen(pw) > 32) return -E_INVAL;
    gettime(&d, &t);
    t = (u16)(t ^ (mk_ticks() * 31));
    for (i = 0; i < 4; i++) { salt[i] = "0123456789abcdef"[(t >> (12 - 4 * i)) & 15]; salt[4 + i] = "0123456789abcdef"[((d ^ (t << 3)) >> (12 - 4 * i)) & 15]; }
    salt[8] = 0;
    pwhash(pw, salt, PW_COUNT, hex);
    fd = open(F_SHADOW, FO_READ);
    if (fd < 0) return fd;
    n = read(fd, sold, sizeof(sold) - 1);
    close(fd);
    if (n < 0) n = 0;
    sold[n] = 0;
    for (p = sold; *p; p = e) {                                 /* the lines of the others as they are, the line of the user new */
        e = p;
        while (*e && *e != '\n') e++;
        if (*e) e++;
        if (!strncmp(p, name, l) && p[l] == ':') continue;
        if (len + (u16)(e - p) + 100 > sizeof(snew)) return -E_NOSPC;
        memcpy(snew + len, p, (u16)(e - p));
        len += (u16)(e - p);
    }
    if (len + l + 80 > sizeof(snew)) return -E_NOSPC;
    memcpy(snew + len, name, l); len += l;
    snew[len++] = ':';
    memcpy(snew + len, salt, 8); len += 8;
    snew[len++] = ':';
    memcpy(snew + len, hex, 64); len += 64;
    snew[len++] = '\n';
    unlink("/etc/shadow.new");
    fd = open("/etc/shadow.new", FO_WRITE | FO_CREATE | FO_TRUNC);
    if (fd < 0) return fd;
    r = write(fd, snew, len);
    close(fd);
    if (r != (int)len) { unlink("/etc/shadow.new"); return -E_IO; }
    chmod("/etc/shadow.new", 0600);
    chown("/etc/shadow.new", 0, 0);
    unlink(F_SHADOW);
    r = rename("/etc/shadow.new", F_SHADOW);
    return r < 0 ? r : 0;
}

int pwcheck(const char *name, const char *pw)
{
    char salt[9], hash[65], hex[65];
    pw_t u;
    if (getpwnam(name, &u) || getshadow(name, salt, hash)) {
        pwhash(pw, "x", PW_COUNT, hex);                 /* (the same time as a right user) */
        return -E_NOENT;
    }
    pwhash(pw, salt, PW_COUNT, hex);
    return strcmp(hex, hash) ? -E_PERM : 0;
}

const char *unamestr(u8 uid, char *buf)
{
    pw_t u;
    if (!getpwuid(uid, &u)) cp(buf, u.name, PW_NAME);
    else utoa_(uid, buf, 10);
    return buf;
}

const char *gnamestr(u8 gid, char *buf)
{
    if (getgrgid(gid, buf)) utoa_(gid, buf, 10);
    return buf;
}

int getruser(u8 *ruid)
{
    msg_t m;
    int r;
    m.op = PM_GETUSER;
    r = mk_rpc(TID_PM, &m, 0);
    if (r) return r;
    *ruid = (u8)m.c;
    return 0;
}

/* a line without the echo (the console in raw mode); BS and DEL erase; the line ends with CR or LF */
int getpass(const char *prompt, char *buf, u8 n)
{
    u8 mode, cs, i = 0, c;
    putstr(prompt);
    fflush();
    tty_getmode(&mode, &cs, NULL, NULL);
    tty_setmode(TM_RAW | TM_CRLF, 0xFF, 0xFF, 0xFF);
    for (;;) {
        if (read(0, &c, 1) != 1) { i = 0; break; }
        if (c == '\r' || c == '\n') break;
        if ((c == 8 || c == 127) && i) i--;
        else if (c >= 32 && i + 1 < n) buf[i++] = (char)c;
    }
    buf[i] = 0;
    tty_setmode(mode, 0xFF, 0xFF, 0xFF);
    putstr("\n");
    return i;
}
#endif
