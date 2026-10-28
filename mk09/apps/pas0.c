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
#define STRMAX  160

#define MAXINC  3                  /* the program, a unit that it uses, a unit that that unit uses; or include files */
static rfile files[MAXINC];
static u8 cur;                     /* the file that is read now */
#define in files[cur]
typedef struct {
    char name[44];
    u16 line, pend;
    int ch;
    u16 prev, bits;
    u8 unit, iface, skip, hdr, drop, parens, depth, pending;     /* (the state of the change of a unit into declarations of the program) */
} fstate_t;
static fstate_t fs[MAXINC];
static char srcdir[40];
static char incname[44];
static u8 inc_pending;
static char unames[6][44];
static u8 nuses, uses_on;
static u16 lastline;
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

static u16 stack_new, heap_new;     /* the stack and the heap of the program: {$M stack,heap}, to be written */

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
            if (ch == ',') {
                nextch();
                v = 0;
                while (ch >= '0' && ch <= '9') { v = (u16)(v * 10 + ch - '0'); nextch(); }
                heap_new = v;
            }
        } else if (ch == 'I' || ch == 'i') {          /* {$I name}: the file is read here ({$I+} and {$I-} are not includes) */
            u8 n = 0;
            nextch();
            while (ch == ' ') nextch();
            while (ch > ' ' && ch != '}' && n < 40) { incname[n++] = (char)ch; nextch(); }
            incname[n] = 0;
            if (incname[0] && incname[0] != '+' && incname[0] != '-') inc_pending = 1;
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

static u8 ieq(const char *a, const char *b)          /* the same file name (without the directory), whatever the case (the file system does not care) */
{
    const char *p;
    for (p = a; *p; p++) if (*p == '/') a = p + 1;
    for (p = b; *p; p++) if (*p == '/') b = p + 1;
    while (*a && *b) {
        char x = *a++, y = *b++;
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
    }
    return *a == *b;
}

static void put_file(const char *n)
{
    w_putc(&out, TK_FILE);
    w_putc(&out, (int)strlen(n));
    w_puts(&out, n);
    lastline = 0;
}

/* a file is read from here on (name, or name.pas; the directory of the program, then the current one, then /LIB); 0 if there is none */
static u8 push_file(const char *name, u8 unit)
{
    char path[84];
    u8 k, j;
    if (cur + 1 >= MAXINC) err("files are nested too deep");
    for (k = 0; k < 3; k++) {
        path[0] = 0;
        if (k == 0) strcpy(path, srcdir); else if (k == 2) strcpy(path, "/LIB/");
        strcat(path, name);
        if (!strchr(name, '.')) strcat(path, ".pas");
        for (j = 0; j <= cur; j++) if (ieq(fs[j].name, path)) break;       /* (a file that is being read is not read again: crt.pas that uses crt) */
        if (j <= cur) continue;
        if (!r_open(&files[cur + 1], path)) break;
    }
    if (k == 3) return 0;
    fs[cur].line = line; fs[cur].ch = ch; fs[cur].pend = pend;
    cur++;
    memset(&fs[cur], 0, sizeof(fstate_t));
    strncpy(fs[cur].name, path, 42);
    fs[cur].unit = unit;
    line = 1;
    pend = 0;
    ch = r_getc(&in);
    put_file(fs[cur].name);
    return 1;
}

static void pop_file(void)
{
    r_close(&in);
    cur--;
    line = fs[cur].line; ch = fs[cur].ch; pend = fs[cur].pend;
    put_file(fs[cur].name);
}

static void next(void)
{
    if (pend) { tok = pend; pend = 0; return; }
    for (;;) {
        while (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') nextch();
        if (inc_pending) { inc_pending = 0; if (!push_file(incname, 0)) err("cannot open the include file"); continue; }
        if (ch < 0 && cur) { pop_file(); continue; }
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

static void put_tok(void)
{
    if (stack_new) { w_putc(&out, TK_STACK); put16(stack_new); stack_new = 0; }
    if (heap_new) { w_putc(&out, TK_HEAP); put16(heap_new); heap_new = 0; }
    if (tokline != lastline) { w_putc(&out, TK_LINE); put16(tokline); lastline = tokline; }
    w_putc(&out, tok);
    if (tok == T_NUM) { put16((u16)tokv); put16(tokh); }
    else if (tok == T_ID || tok == T_STR || tok == T_REAL) put_text();
}

/* (a name has 12 characters at most here, so implementation and initialization are "implementati" and "initializati")
 * "uses a, b;" reads the units (a name without a file, like crt, is ignored).  A unit is turned into declarations of the program: unit, interface, implementation,
 * the initialization part and the last end. are taken away, and the headers of the interface become forward declarations. */
static u8 want(void)
{
    fstate_t *f = &fs[cur];
    u16 t = tok;
    if (uses_on) {
        if (t == T_ID) { if (nuses < 6) { strncpy(unames[nuses], toks, 42); nuses++; } }
        else if (t == ';' || t == T_EOF) { uses_on = 0; while (nuses) push_file(unames[--nuses], 1); }
        return 0;
    }
    if (t == T_ID && !strcmp(toks, "uses")) { uses_on = 1; nuses = 0; return 0; }
    if (f->unit) {
        if (f->skip) return 0;
        if (f->drop) { f->drop--; return 0; }
        if (t == T_ID) {
            if (!f->prev && !strcmp(toks, "unit")) { f->drop = 2; return 0; }
            if (!strcmp(toks, "interface")) { f->iface = 1; return 0; }
            if (!strcmp(toks, "implementati")) { f->iface = 0; return 0; }
            if (!strcmp(toks, "initializati")) { f->skip = 1; return 0; }
        }
        switch (t) {
        case K_PROCEDURE:
        case K_FUNCTION:
            if ((f->prev == ';' || !f->prev) && !f->depth) { if (f->iface) f->hdr = 1; else f->pending++; }
            break;
        case '(': f->parens++; break;
        case ')': f->parens--; break;
        case ';':
            if (f->hdr && !f->parens) {
                put_tok();
                w_putc(&out, K_FORWARD);
                w_putc(&out, ';');
                f->hdr = 0;
                f->prev = ';';
                return 0;
            }
            break;
        case K_BEGIN:
            if (!f->depth && !f->pending) { f->skip = 1; return 0; }
            if (!f->depth) f->pending--;
            f->bits <<= 1; f->depth++;
            break;
        case K_RECORD:
            f->bits = (u16)((f->bits << 1) | 1); f->depth++;
            break;
        case K_CASE:
            if (!(f->depth && (f->bits & 1))) { f->bits <<= 1; f->depth++; }       /* (a case in a record has no end of its own) */
            break;
        case K_END:
            if (!f->depth) { f->skip = 1; return 0; }
            f->bits >>= 1; f->depth--;
            break;
        }
    }
    f->prev = t;
    return 1;
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
    {
        u8 k = (u8)strlen(src);
        while (k && src[k - 1] != '/') k--;
        if (k > 38) k = 0;
        memcpy(srcdir, src, k);
        srcdir[k] = 0;
    }
    if (r_open(&in, src)) { puts("pas0: cannot open the input"); return 2; }
    if (w_open(&out, dst)) { puts("pas0: cannot create the output"); return 2; }
    strncpy(fs[0].name, src, 42);
    put_file(fname);
    ch = r_getc(&in);
    for (;;) {
        next();
        if (tok == T_EOF) { put_tok(); break; }
        if (want()) put_tok();
    }
    w_close(&out);
    r_close(&in);
    return run_tail(argc, argv, tail);
}
