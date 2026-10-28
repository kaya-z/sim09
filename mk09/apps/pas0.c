/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pas0 - the first stage of the Pascal compiler of MK09: Pascal text in, tokens out (apps/pastok.h, docs/PASCAL.md)
 *
 *   pas0 -o out.tok file.pas
 *
 * It takes away the comments and the blanks, makes names lower case, finds the keywords, turns numbers into binary and reads the strings, so that pas1 (which
 * has no room to spare in the transient area of MK09) need not.
 */
#include "ccio.h"
#include "pastok.h"

#define NAMEMAX 12
#define STRMAX  255

static rfile in;
static wfile out;
static char fname[80];
static u16 line = 1, tokline;
static int ch;
static u16 tok;
static int tokv;
static u16 tokh;
static char toks[STRMAX + 2];
static u16 toklen;
static u16 pend;               /* a token that was found while reading a number (the .. of 1..5) */

static char *keywords[] = {
    "and", "array", "begin", "case", "const", "div", "do", "downto", "else", "end", "file", "for", "forward", "function", "goto", "if", "in", "label",
    "mod", "nil", "not", "of", "or", "packed", "procedure", "program", "record", "repeat", "set", "then", "to", "type", "until", "var", "while", "with",
    "xor", "shl", "shr", 0
};

static void err(const char *msg)
{
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", fname, line, msg);
    exit(1);
#else
    char nb[7];
    putstr(fname); putchar(':'); utoa_(line, nb, 10); putstr(nb); putstr(": "); putstr(msg); putchar('\n');
    exit(1);
#endif
}

static void nextch(void) { ch = r_getc(&in); if (ch == '\n') line++; }

static int isidc(int c, u8 first)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return !first && c >= '0' && c <= '9';
}

static int hexval(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static u16 stack_new;               /* the stack of the program: {$M n}, to be written */

static void skip_comment(int close2)       /* after { or (* */
{
    if (!close2 && ch == '$') {                /* a directive: {$M 4096} sets the stack */
        nextch();
        if (ch == 'M' || ch == 'm') {
            u16 v = 0;
            nextch();
            while (ch == ' ') nextch();
            while (ch >= '0' && ch <= '9') { v = (u16)(v * 10 + ch - '0'); nextch(); }
            if (v) stack_new = v;
        }
    }
    for (;;) {
        if (ch < 0) err("a comment is not closed");
        if (close2) {
            if (ch == '*') { nextch(); if (ch == ')') { nextch(); return; } continue; }
        } else if (ch == '}') { nextch(); return; }
        nextch();
    }
}

static void add_tok(int c) { if (toklen < STRMAX) toks[toklen++] = (char)c; else err("a string is too long"); }

static void next(void)
{
    if (pend) { tok = pend; pend = 0; return; }
    for (;;) {
        while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') nextch();
        if (ch == '{') { nextch(); skip_comment(0); continue; }
        if (ch == '(') {
            nextch();
            if (ch == '*') { nextch(); skip_comment(1); continue; }
            tok = '(';
            return;
        }
        if (ch == '/') {
            nextch();
            if (ch == '/') { while (ch >= 0 && ch != '\n') nextch(); continue; }
            tok = '/';
            return;
        }
        break;
    }
    tokline = line;
    if (ch < 0) { tok = T_EOF; return; }
    if (isidc(ch, 1)) {
        u8 k = 0;
        while (isidc(ch, 0)) {
            int c = ch;
            if (c >= 'A' && c <= 'Z') c += 32;
            if (k < NAMEMAX) toks[k++] = (char)c;
            nextch();
        }
        toks[k] = 0;
        toklen = k;
        tok = T_ID;
        for (k = 0; keywords[k]; k++) if (!strcmp(keywords[k], toks)) { tok = (u16)(K_AND + k); break; }
        return;
    }
    if ((ch >= '0' && ch <= '9') || ch == '$') {
        u16 v = 0, vh = 0, a, b, lo, mid;
        u8 isreal = 0, n = 0;
        if (ch == '$') {
            nextch();
            if (hexval(ch) < 0) err("a hexadecimal number is expected");
            while (hexval(ch) >= 0) { vh = (u16)((vh << 4) | (v >> 12)); v = (u16)(v * 16 + hexval(ch)); nextch(); }
        } else {
            while (ch >= '0' && ch <= '9') {
                a = v & 255; b = v >> 8;
                lo = (u16)(a * 10 + ch - '0');
                mid = (u16)(b * 10 + (lo >> 8));
                v = (u16)((lo & 255) | (mid << 8));
                vh = (u16)(vh * 10 + (mid >> 8));
                if (n < 30) toks[n++] = (char)ch;
                nextch();
            }
            if (ch == '.') {
                nextch();
                if (ch >= '0' && ch <= '9') {
                    isreal = 1;
                    toks[n++] = '.';
                    while (ch >= '0' && ch <= '9') { if (n < 40) toks[n++] = (char)ch; nextch(); }
                } else if (ch == '.') { nextch(); pend = T_DOTDOT; }
                else pend = '.';
            }
            if (!pend && (ch == 'e' || ch == 'E')) {
                isreal = 1;
                toks[n++] = 'e';
                nextch();
                if (ch == '+' || ch == '-') { toks[n++] = (char)ch; nextch(); }
                if (ch < '0' || ch > '9') err("an exponent is expected");
                while (ch >= '0' && ch <= '9') { if (n < 46) toks[n++] = (char)ch; nextch(); }
            }
            if (isreal) {                      /* (the text stays in toks: the program converts it when it runs: atof) */
                toks[n] = 0;
                toklen = n;
                tok = T_REAL;
                return;
            }
        }
        tokv = (int)v;
        tokh = vh;
        tok = T_NUM;
        return;
    }
    if (ch == '\'' || ch == '#') {
        toklen = 0;
        for (;;) {
            if (ch == '\'') {
                nextch();
                for (;;) {
                    if (ch < 0 || ch == '\n') err("a string is not closed");
                    if (ch == '\'') { nextch(); if (ch != '\'') break; }
                    add_tok(ch);
                    nextch();
                }
            } else if (ch == '#') {
                int v = 0;
                nextch();
                while (ch >= '0' && ch <= '9') { v = v * 10 + ch - '0'; nextch(); }
                add_tok(v);
            } else break;
        }
        toks[toklen] = 0;
        tok = T_STR;
        return;
    }
    tok = (u16)ch;
    nextch();
    if (tok == ':' && ch == '=') { nextch(); tok = T_ASSIGN; }
    else if (tok == '<' && ch == '=') { nextch(); tok = T_LE; }
    else if (tok == '<' && ch == '>') { nextch(); tok = T_NE; }
    else if (tok == '>' && ch == '=') { nextch(); tok = T_GE; }
    else if (tok == '.' && ch == '.') { nextch(); tok = T_DOTDOT; }
}


static void put16(u16 v) { w_putc(&out, v >> 8); w_putc(&out, v & 255); }

static void put_text(void)
{
    u16 i;
    w_putc(&out, toklen);
    for (i = 0; i < toklen; i++) w_putc(&out, toks[i]);
}

int main(int argc, char **argv)
{
    int i, tail = tail_start(argc, argv);
    const char *src = NULL, *dst = NULL;
    u16 last = 0;
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < tail) dst = argv[++i];
        else if (argv[i][0] == '-') { puts("pas0: unknown option"); return 2; }
        else src = argv[i];
    }
    if (!src || !dst) { puts("usage: pas0 -o out.tok file.pas"); return 2; }
    strncpy(fname, src, 78);
    if (r_open(&in, src)) { puts("pas0: cannot open the input"); return 2; }
    if (w_open(&out, dst)) { puts("pas0: cannot create the output"); return 2; }
    w_putc(&out, TK_FILE);
    w_putc(&out, (int)strlen(fname));
    w_puts(&out, fname);
    ch = r_getc(&in);
    for (;;) {
        next();
        if (stack_new) { w_putc(&out, TK_STACK); put16(stack_new); stack_new = 0; }
        if (tokline != last) { w_putc(&out, TK_LINE); put16(tokline); last = tokline; }
        w_putc(&out, tok);
        if (tok == T_NUM) { put16((u16)tokv); put16(tokh); }
        else if (tok == T_ID || tok == T_STR || tok == T_REAL) put_text();
        else if (tok == T_EOF) break;
    }
    w_close(&out);
    r_close(&in);
    return run_tail(argc, argv, tail);
}
