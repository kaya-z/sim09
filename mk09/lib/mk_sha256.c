/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * lib/mk_sha256.c - part of the MK09 C library (2.0): SHA-256 of a short message, and the hash of a password.
 *
 * pwhash(): h = SHA-256(salt || password); then COUNT times  h = SHA-256(h || salt || password).  The result is 64 hexadecimal digits.
 * The same function is in tools/mkusers.py (the password file of the disk image is made there).  The salt has up to 8 and the password up to 32 characters.
 * The messages are short (at most 72 bytes), so the padding is made in one buffer of two blocks.
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"

#ifdef MK09_V2
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static const u32 K[64] = {
    0x428a2f98UL, 0x71374491UL, 0xb5c0fbcfUL, 0xe9b5dba5UL, 0x3956c25bUL, 0x59f111f1UL, 0x923f82a4UL, 0xab1c5ed5UL,
    0xd807aa98UL, 0x12835b01UL, 0x243185beUL, 0x550c7dc3UL, 0x72be5d74UL, 0x80deb1feUL, 0x9bdc06a7UL, 0xc19bf174UL,
    0xe49b69c1UL, 0xefbe4786UL, 0x0fc19dc6UL, 0x240ca1ccUL, 0x2de92c6fUL, 0x4a7484aaUL, 0x5cb0a9dcUL, 0x76f988daUL,
    0x983e5152UL, 0xa831c66dUL, 0xb00327c8UL, 0xbf597fc7UL, 0xc6e00bf3UL, 0xd5a79147UL, 0x06ca6351UL, 0x14292967UL,
    0x27b70a85UL, 0x2e1b2138UL, 0x4d2c6dfcUL, 0x53380d13UL, 0x650a7354UL, 0x766a0abbUL, 0x81c2c92eUL, 0x92722c85UL,
    0xa2bfe8a1UL, 0xa81a664bUL, 0xc24b8b70UL, 0xc76c51a3UL, 0xd192e819UL, 0xd6990624UL, 0xf40e3585UL, 0x106aa070UL,
    0x19a4c116UL, 0x1e376c08UL, 0x2748774cUL, 0x34b0bcb5UL, 0x391c0cb3UL, 0x4ed8aa4aUL, 0x5b9cca4fUL, 0x682e6ff3UL,
    0x748f82eeUL, 0x78a5636fUL, 0x84c87814UL, 0x8cc70208UL, 0x90befffaUL, 0xa4506cebUL, 0xbef9a3f7UL, 0xc67178f2UL
};

static void block(u32 *h, const u8 *p)
{
    u32 w[64], a, b, c, d, e, f, g, hh, t1, t2;
    u8 i;
    for (i = 0; i < 16; i++, p += 4)
        w[i] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
    for (i = 16; i < 64; i++) {
        t1 = w[i - 15];
        t2 = w[i - 2];
        w[i] = w[i - 16] + w[i - 7] + (ROR(t1, 7) ^ ROR(t1, 18) ^ (t1 >> 3)) + (ROR(t2, 17) ^ ROR(t2, 19) ^ (t2 >> 10));
    }
    a = h[0]; b = h[1]; c = h[2]; d = h[3]; e = h[4]; f = h[5]; g = h[6]; hh = h[7];
    for (i = 0; i < 64; i++) {
        t1 = hh + (ROR(e, 6) ^ ROR(e, 11) ^ ROR(e, 25)) + ((e & f) ^ (~e & g)) + K[i] + w[i];
        t2 = (ROR(a, 2) ^ ROR(a, 13) ^ ROR(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}

/* SHA-256 of n (at most 119) bytes */
void sha256(const u8 *msg, u8 n, u8 *out)
{
    static u8 buf[128];
    u32 h[8];
    u8 i, blocks = (u8)(n < 56 ? 1 : 2);
    u16 bits = (u16)n * 8;
    h[0] = 0x6a09e667UL; h[1] = 0xbb67ae85UL; h[2] = 0x3c6ef372UL; h[3] = 0xa54ff53aUL;
    h[4] = 0x510e527fUL; h[5] = 0x9b05688cUL; h[6] = 0x1f83d9abUL; h[7] = 0x5be0cd19UL;
    memset(buf, 0, 128);
    memcpy(buf, msg, n);
    buf[n] = 0x80;
    buf[blocks * 64 - 2] = (u8)(bits >> 8);
    buf[blocks * 64 - 1] = (u8)bits;
    block(h, buf);
    if (blocks == 2) block(h, buf + 64);
    for (i = 0; i < 32; i++) out[i] = (u8)(h[i >> 2] >> (24 - 8 * (i & 3)));
}

/* the hash of a password with a salt, 64 hexadecimal digits and a NUL in hex[65]; count: the repeats */
void pwhash(const char *pw, const char *salt, u8 count, char *hex)
{
    u8 msg[72], h[32], i, ls = (u8)strlen(salt), lp = (u8)strlen(pw), n;
    if (ls > 8) ls = 8;
    if (lp > 32) lp = 32;
    memcpy(msg, salt, ls);
    memcpy(msg + ls, pw, lp);
    sha256(msg, (u8)(ls + lp), h);
    while (count--) {
        memcpy(msg, h, 32);
        memcpy(msg + 32, salt, ls);
        memcpy(msg + 32 + ls, pw, lp);
        n = (u8)(32 + ls + lp);
        sha256(msg, n, h);
    }
    for (i = 0; i < 32; i++) {
        hex[2 * i] = "0123456789abcdef"[h[i] >> 4];
        hex[2 * i + 1] = "0123456789abcdef"[h[i] & 15];
    }
    hex[64] = 0;
}
#endif
