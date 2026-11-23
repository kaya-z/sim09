/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* pwd.h - the user database of MK09 2.0: /etc/passwd (name:uid:gid:home:shell), /etc/group (name:gid), /etc/shadow (name:salt:hash; root only) */
#ifndef MK_PWD_H
#define MK_PWD_H

#include "types.h"

#define PW_COUNT    2           /* the repeats of the hash of a password (tools/mkusers.py has the same number) */
#define PW_NAME     12
#define F_PASSWD    "/etc/passwd"
#define F_GROUP     "/etc/group"
#define F_SHADOW    "/etc/shadow"

typedef struct pw {
    char name[PW_NAME];
    u8   uid, gid;
    char home[24];
    char shell[20];
} pw_t;

int  getpwnam(const char *name, pw_t *pw);          /* 0 or a negative error */
int  getpwuid(u8 uid, pw_t *pw);
int  getgrnam(const char *name, u8 *gid);
int  getgrgid(u8 gid, char *name);                  /* name[PW_NAME] */
int  getshadow(const char *name, char *salt, char *hash);   /* salt[9], hash[65] */
int  getpass(const char *prompt, char *buf, u8 n);  /* read a line without the echo */
int  pwexpired(const char *name);                   /* 1: the password must be changed at the next login (the 4th field of the line in /etc/shadow is 1) */
int  pwset(const char *name, const char *pw);       /* a new password (a new salt): /etc/shadow is written again, the "must change" mark goes; root only (0 or a negative error) */
int  pwcheck(const char *name, const char *pw);     /* 0: the password is right; negative: no such user or wrong */
const char *unamestr(u8 uid, char *buf);            /* the name of the user, or the number (buf[PW_NAME]) */
const char *gnamestr(u8 gid, char *buf);
int  getruser(u8 *ruid);                            /* the user who started this program (not the owner of a setuid file) */
void pwhash(const char *pw, const char *salt, u8 count, char *hex);
void sha256(const u8 *msg, u8 n, u8 *out);

#endif
