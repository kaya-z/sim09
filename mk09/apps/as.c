/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * as - a 6809 assembler that runs on MK09 (see docs/MANUAL.md, "Programs written in assembler")
 *
 *   as [-r] [-s] [-o out] file.s
 *
 * The output is an MX executable (the format of lib/crt0app.s): a 16 byte header, the program, and the
 * relocation table.  The start code of crt0app.s is put in front of the program by the assembler itself,
 * so a program is a single source file without a linker.  Every 16 bit value that is an address of the
 * program (a label, or a label plus or minus a number) gets an entry in the relocation table; this is how
 * the program can be loaded at any address that is a multiple of 256.
 *
 *   -o out   the output file (default: the source name with ".mx")
 *   -r       a raw image: no start code, no header, no table; the program starts at address 0
 *   -s       print the symbol table
 *
 * The language is the one of lwasm in its simple form: "label  mnemonic  operand  ; comment", `*` or `;` at the
 * start of a line is a comment, a name at the start of a line is a label (an optional ':' is dropped).
 *   Addressing   #n  <n (direct)  >n (extended)  n  n,R  ,R  ,R+  ,R++  ,-R  ,--R  A,R  B,R  D,R  n,PC  n,PCR  [..]
 *   Directives   equ  set  fcb  fdb  fcc  fcs  rmb  zmb  bss  code  stack  runtime  end
 *   Expressions  numbers (123 $7F 0x7F %101 'A), names, * (the address of the instruction), ( ) + - * / % & | ^ << >> ~
 *   Names        '@name' is local to the last name without '@'; case is significant in names, not in mnemonics
 * Branches are short (BRA ...) or long (LBRA ...); the assembler never changes the length of an instruction
 * by itself, except for an offset in indexed addressing ("n,X": 5, 8 or 16 bits by the value of n when it is
 * known when the line is read, 16 bits if it is not) and "n,PCR" (8 bits if the label was defined before and is near).
 */
#ifdef AS_HOST
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef uint8_t u8;
typedef uint16_t u16;
typedef int8_t s8;
#define NULL ((void *)0)
#else
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#endif

#ifdef AS_HOST
#define LOG printf
static FILE *fin, *fout;
static int rd(void *b, int n) { return (int)fread(b, 1, n, fin); }
static int wr(const void *b, int n) { return fwrite(b, 1, n, fout) == (size_t)n ? n : -1; }
#else
#define LOG printf
static int fin, fout;
static int rd(void *b, int n) { return read(fin, b, n); }
static int wr(const void *b, int n) { return write(fout, b, n); }
#endif

#define NSYM    500
#define NHASH   64
#define NREL    2600
#define LINEMAX 128
#define NAMEMAX 17

/* ---------------------------------------------------------------- symbols */
#define F_DEF 1
#define F_REL 2     /* the value is an address of the program */
#define F_BSS 4     /* the address is in the bss: the size of the image is added when it is known */
typedef struct sym {
    char name[NAMEMAX + 1];
    int  val;
    u16  defline;
    u8   flags;
    u16  next;      /* index + 1 of the next symbol with the same hash, 0 = end */
} sym_t;
static sym_t syms[NSYM];
static u16 hashtab[NHASH];
static u16 nsym;

static u8 hashof(const char *s)
{
    u8 h = 0;
    while (*s) h = (u8)(h * 3 + (u8)*s++);
    return h & (NHASH - 1);
}

static int lookup(const char *n, u8 create)
{
    u8 h = hashof(n);
    u16 i = hashtab[h];
    while (i) {
        if (!strcmp(syms[i - 1].name, n)) return i - 1;
        i = syms[i - 1].next;
    }
    if (!create || nsym >= NSYM) return -1;
    strcpy(syms[nsym].name, n);
    syms[nsym].flags = 0;
    syms[nsym].val = 0;
    syms[nsym].defline = 0;
    syms[nsym].next = hashtab[h];
    nsym++;
    hashtab[h] = nsym;
    return nsym - 1;
}

/* ---------------------------------------------------------------- state */
static u8 pass;
static u8 errors, e1, noout, raw, showsyms, userrt, inbss;
static int pc, bpc, bssmax, imgend, stksz = 512;
static u16 lineno;
static u16 scope;
static const char *curfile;
static u16 srcline;
static u16 uline;               /* the line of the user's file */
static u8 srcmode;              /* 0 start code, 1 the file, 2 the runtime, 3 end */

/* the errors of the first pass are counted only (the second pass finds them again, and the undefined names as well) */
static void err(const char *msg)
{
    if (pass == 1) { e1++; return; }
    if (errors < 20) {
        if (srcmode == 1) LOG("%s:%u: %s\n", curfile, uline, msg);
        else LOG("%s: (%s of the assembler) %s\n", curfile, srcmode == 0 ? "start code" : "runtime", msg);
    }
    errors++;
}

/* ---------------------------------------------------------------- expressions */
static u8 xrel;     /* 1: the value is an address of the program (a label or label +/- number) */
static u8 xfwd;     /* a name that is not defined yet at this line was used (its value may not be the final one) */
static u8 xbss;     /* the address is in the bss */
static u8 xundef;   /* a name has no value at all (pass 1: not yet seen) */

static int isnamec(char c, u8 first)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '.' || c == '@' || c == '?') return 1;
    return !first && c >= '0' && c <= '9';
}

static void mkname(char *dst, const char *src, u8 n)
{
    u8 i;
    if (src[0] == '@') {            /* local: '@' + the scope + the name */
        const char hx[] = "0123456789ABCDEF";
        dst[0] = '@';
        dst[1] = hx[(scope >> 12) & 15];
        dst[2] = hx[(scope >> 8) & 15];
        dst[3] = hx[(scope >> 4) & 15];
        dst[4] = hx[scope & 15];
        for (i = 0; i < n - 1 && i < NAMEMAX - 5; i++) dst[5 + i] = src[1 + i];
        dst[5 + i] = 0;
    } else {
        for (i = 0; i < n && i < NAMEMAX; i++) dst[i] = src[i];
        dst[i] = 0;
    }
}

static u8 hexv(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 255;
}

static void skipws(const char **p)
{
    while (**p == ' ' || **p == '\t') (*p)++;
}

static int unary(const char **p);
static int binop(const char **p, u8 lv);

static int primary(const char **p)
{
    const char *s = *p;
    int v = 0;
    skipws(&s);
    xrel = 0; xbss = 0;
    if (*s == '(') {
        s++;
        v = binop(&s, 0);
        skipws(&s);
        if (*s != ')') err("missing )"); else s++;
    } else if (*s == '*') {
        s++;
        v = pc; xrel = 1;
        if (inbss) { v = bpc; xbss = 1; }
    } else if (*s == '\'') {
        s++;
        v = (u8)*s;
        if (*s) s++;
        if (*s == '\'') s++;
    } else if (*s == '$' && hexv(s[1]) != 255) {
        s++;
        while (hexv(*s) != 255) v = v * 16 + hexv(*s++);
    } else if (*s == '%' && (s[1] == '0' || s[1] == '1')) {
        s++;
        while (*s == '0' || *s == '1') v = v * 2 + (*s++ - '0');
    } else if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') && hexv(s[2]) != 255) {
        s += 2;
        while (hexv(*s) != 255) v = v * 16 + hexv(*s++);
    } else if (*s >= '0' && *s <= '9') {
        while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    } else if (isnamec(*s, 1)) {
        char nm[NAMEMAX + 1];
        const char *b = s;
        int k;
        while (isnamec(*s, 0)) s++;
        mkname(nm, b, (u8)(s - b));
        k = lookup(nm, 1);
        if (k < 0) { err("too many names"); }
        else {
            sym_t *y = &syms[k];
            if (!(y->flags & F_DEF)) { xundef = 1; xfwd = 1; if (pass == 2) err("undefined name"); }
            else {
                if (y->defline >= srcline) xfwd = 1;     /* defined later (or on this line) */
                v = y->val;
                if (y->flags & F_REL) xrel = 1;
                if (y->flags & F_BSS) { xbss = 1; if (pass == 2) v += imgend; }
            }
        }
    } else {
        err("bad expression");
        if (*s) s++;
    }
    *p = s;
    return v;
}

static int unary(const char **p)
{
    const char *s = *p;
    int v;
    skipws(&s);
    if (*s == '-') { s++; v = -unary(&s); if (xrel) err("a negated address"); }
    else if (*s == '+') { s++; v = unary(&s); }
    else if (*s == '~') { s++; v = ~unary(&s); if (xrel) err("an address in ~"); }
    else v = primary(&s);
    *p = s;
    return v;
}

/* levels: 0 | ^   1 &   2 << >>   3 + -   4 * / % */
static int binop(const char **p, u8 lv)
{
    const char *s = *p;
    int a, b;
    u8 ar, ab, br, bb;
    if (lv == 5) return unary(p);
    a = binop(&s, lv + 1);
    for (;;) {
        char c;
        skipws(&s);
        c = *s;
        if (lv == 0 && (c == '|' || c == '^')) s++;
        else if (lv == 1 && c == '&') s++;
        else if (lv == 2 && ((c == '<' && s[1] == '<') || (c == '>' && s[1] == '>'))) s += 2;
        else if (lv == 3 && (c == '+' || c == '-')) s++;
        else if (lv == 4 && (c == '*' || c == '/' || c == '%')) s++;
        else break;
        ar = xrel; ab = xbss;
        b = binop(&s, lv + 1);
        br = xrel; bb = xbss;
        if (lv == 3) {
            if (c == '+') {
                if (ar && br) err("two addresses added");
                a = a + b;
                if (br) { ar = 1; ab = bb; }
            } else {
                if (br && !ar) err("an address subtracted from a number");
                a = a - b;
                if (ar && br) { ar = 0; ab = 0; }          /* the distance of two addresses */
            }
            xrel = ar; xbss = ab;
        } else {
            if (ar || br) err("an address in an expression that needs numbers");
            if (lv == 0) a = c == '|' ? (a | b) : (a ^ b);
            else if (lv == 1) a = a & b;
            else if (lv == 2) { if (c == '<') { while (b-- > 0) a = a << 1; } else { u16 u = (u16)a; while (b-- > 0) u >>= 1; a = (int)u; } }
            else if (c == '*') a = a * b;
            else if (b == 0) { err("division by zero"); a = 0; }
            else if (c == '/') a = a / b;
            else a = a % b;
            xrel = 0; xbss = 0;
        }
    }
    *p = s;
    return a;
}

static int expr(const char **p)
{
    int v;
    xundef = 0; xfwd = 0;
    v = binop(p, 0);
    return v;
}

/* ---------------------------------------------------------------- output */
static u8 obuf[256];
static u16 olen;
static u16 relpos[NREL];
static u16 nrel;
static int opos;            /* offset of the next byte in the image */

static void oflush(void)
{
    if (olen) { if (wr(obuf, olen) < 0) { err("write error"); } olen = 0; }
}

static void emit(u8 b)
{
    if (!inbss) {
        if (pass == 2 && !errors && !noout) {
            obuf[olen++] = b;
            if (olen == sizeof(obuf)) oflush();
        }
        opos++;
    }
}

/* ---------------------------------------------------------------- tables */
enum { T_INH, T_MEM, T_ACC8, T_ACC16, T_ST8, T_ST16, T_LEA, T_BR, T_LBR, T_RR, T_STK, T_IMM8 };
typedef struct op {
    const char *name;
    u8 type;
    u16 code;
} op_t;

static const op_t optab[] = {
    {"NOP", T_INH, 0x12}, {"SYNC", T_INH, 0x13}, {"DAA", T_INH, 0x19}, {"SEX", T_INH, 0x1D}, {"RTS", T_INH, 0x39},
    {"ABX", T_INH, 0x3A}, {"RTI", T_INH, 0x3B}, {"MUL", T_INH, 0x3D}, {"SWI", T_INH, 0x3F}, {"SWI2", T_INH, 0x103F},
    {"SWI3", T_INH, 0x113F},
    {"NEGA", T_INH, 0x40}, {"COMA", T_INH, 0x43}, {"LSRA", T_INH, 0x44}, {"RORA", T_INH, 0x46}, {"ASRA", T_INH, 0x47},
    {"ASLA", T_INH, 0x48}, {"LSLA", T_INH, 0x48}, {"ROLA", T_INH, 0x49}, {"DECA", T_INH, 0x4A}, {"INCA", T_INH, 0x4C},
    {"TSTA", T_INH, 0x4D}, {"CLRA", T_INH, 0x4F},
    {"NEGB", T_INH, 0x50}, {"COMB", T_INH, 0x53}, {"LSRB", T_INH, 0x54}, {"RORB", T_INH, 0x56}, {"ASRB", T_INH, 0x57},
    {"ASLB", T_INH, 0x58}, {"LSLB", T_INH, 0x58}, {"ROLB", T_INH, 0x59}, {"DECB", T_INH, 0x5A}, {"INCB", T_INH, 0x5C},
    {"TSTB", T_INH, 0x5D}, {"CLRB", T_INH, 0x5F},
    {"NEG", T_MEM, 0x00}, {"COM", T_MEM, 0x03}, {"LSR", T_MEM, 0x04}, {"ROR", T_MEM, 0x06}, {"ASR", T_MEM, 0x07},
    {"ASL", T_MEM, 0x08}, {"LSL", T_MEM, 0x08}, {"ROL", T_MEM, 0x09}, {"DEC", T_MEM, 0x0A}, {"INC", T_MEM, 0x0C},
    {"TST", T_MEM, 0x0D}, {"JMP", T_MEM, 0x0E}, {"CLR", T_MEM, 0x0F},
    {"SUBA", T_ACC8, 0x80}, {"CMPA", T_ACC8, 0x81}, {"SBCA", T_ACC8, 0x82}, {"ANDA", T_ACC8, 0x84}, {"BITA", T_ACC8, 0x85},
    {"LDA", T_ACC8, 0x86}, {"EORA", T_ACC8, 0x88}, {"ADCA", T_ACC8, 0x89}, {"ORA", T_ACC8, 0x8A}, {"ADDA", T_ACC8, 0x8B},
    {"SUBB", T_ACC8, 0xC0}, {"CMPB", T_ACC8, 0xC1}, {"SBCB", T_ACC8, 0xC2}, {"ANDB", T_ACC8, 0xC4}, {"BITB", T_ACC8, 0xC5},
    {"LDB", T_ACC8, 0xC6}, {"EORB", T_ACC8, 0xC8}, {"ADCB", T_ACC8, 0xC9}, {"ORB", T_ACC8, 0xCA}, {"ADDB", T_ACC8, 0xCB},
    {"SUBD", T_ACC16, 0x83}, {"ADDD", T_ACC16, 0xC3}, {"CMPX", T_ACC16, 0x8C}, {"LDX", T_ACC16, 0x8E}, {"LDD", T_ACC16, 0xCC},
    {"LDU", T_ACC16, 0xCE}, {"LDS", T_ACC16, 0x10CE}, {"CMPD", T_ACC16, 0x1083}, {"CMPY", T_ACC16, 0x108C},
    {"LDY", T_ACC16, 0x108E}, {"CMPU", T_ACC16, 0x1183}, {"CMPS", T_ACC16, 0x118C},
    {"STA", T_ST8, 0x97}, {"STB", T_ST8, 0xD7},
    {"STD", T_ST16, 0xDD}, {"STX", T_ST16, 0x9F}, {"STU", T_ST16, 0xDF}, {"STS", T_ST16, 0x10DF}, {"STY", T_ST16, 0x109F},
    {"JSR", T_ST16, 0x9D},
    {"LEAX", T_LEA, 0x30}, {"LEAY", T_LEA, 0x31}, {"LEAS", T_LEA, 0x32}, {"LEAU", T_LEA, 0x33},
    {"BRA", T_BR, 0x20}, {"BRN", T_BR, 0x21}, {"BHI", T_BR, 0x22}, {"BLS", T_BR, 0x23}, {"BCC", T_BR, 0x24}, {"BHS", T_BR, 0x24},
    {"BCS", T_BR, 0x25}, {"BLO", T_BR, 0x25}, {"BNE", T_BR, 0x26}, {"BEQ", T_BR, 0x27}, {"BVC", T_BR, 0x28}, {"BVS", T_BR, 0x29},
    {"BPL", T_BR, 0x2A}, {"BMI", T_BR, 0x2B}, {"BGE", T_BR, 0x2C}, {"BLT", T_BR, 0x2D}, {"BGT", T_BR, 0x2E}, {"BLE", T_BR, 0x2F},
    {"BSR", T_BR, 0x8D},
    {"LBRA", T_LBR, 0x16}, {"LBSR", T_LBR, 0x17}, {"LBRN", T_LBR, 0x1021}, {"LBHI", T_LBR, 0x1022}, {"LBLS", T_LBR, 0x1023},
    {"LBCC", T_LBR, 0x1024}, {"LBHS", T_LBR, 0x1024}, {"LBCS", T_LBR, 0x1025}, {"LBLO", T_LBR, 0x1025}, {"LBNE", T_LBR, 0x1026},
    {"LBEQ", T_LBR, 0x1027}, {"LBVC", T_LBR, 0x1028}, {"LBVS", T_LBR, 0x1029}, {"LBPL", T_LBR, 0x102A}, {"LBMI", T_LBR, 0x102B},
    {"LBGE", T_LBR, 0x102C}, {"LBLT", T_LBR, 0x102D}, {"LBGT", T_LBR, 0x102E}, {"LBLE", T_LBR, 0x102F},
    {"TFR", T_RR, 0x1F}, {"EXG", T_RR, 0x1E},
    {"PSHS", T_STK, 0x34}, {"PULS", T_STK, 0x35}, {"PSHU", T_STK, 0x36}, {"PULU", T_STK, 0x37},
    {"ANDCC", T_IMM8, 0x1C}, {"ORCC", T_IMM8, 0x1A}, {"CWAI", T_IMM8, 0x3C},
    {0, 0, 0}
};

/* ---------------------------------------------------------------- instruction bytes */
static u8 ib[8];
static u8 ilen;
static s8 irel;             /* index in ib of the high byte of an address, or -1 */

static void ibyte(u8 b) { ib[ilen++] = b; }
static void iop(u16 code) { if (code > 255) ibyte((u8)(code >> 8)); ibyte((u8)code); }
static void iword(int v) { ibyte((u8)((u16)v >> 8)); ibyte((u8)v); }

static void nonrel(const char *what)
{
    if (xrel) { err(what); xrel = 0; }
}

/* the end of a name or expression part: a pointer to the first ',' at the top level */
static const char *findcomma(const char *s)
{
    int d = 0;
    for (; *s; s++) {
        if (*s == '\'') { if (s[1]) s++; if (s[1] == '\'') s++; continue; }
        if (*s == '(' || *s == '[') d++;
        else if (*s == ')' || *s == ']') d--;
        else if (*s == ',' && d == 0) return s;
    }
    return 0;
}

static int ucase(int c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }

/* "X", "Y", "U", "S", "PC", "PCR" (case does not matter): returns 0..3 for X Y U S, 4 for PC, 5 for PCR, 255 if it is none of them */
static u8 regidx(const char *s, u8 *len)
{
    int a = ucase(s[0]), b = ucase(s[1]);
    *len = 1;
    if (!isnamec(s[1], 0)) {
        if (a == 'X') return 0;
        if (a == 'Y') return 1;
        if (a == 'U') return 2;
        if (a == 'S') return 3;
    }
    if (a == 'P' && b == 'C') {
        *len = 2;
        if (ucase(s[2]) == 'R' && !isnamec(s[3], 0)) { *len = 3; return 5; }
        if (!isnamec(s[2], 0)) return 4;
    }
    return 255;
}

/* an indexed operand (the text after '[' or not): fills ib, with the post byte; op is already in ib */
static void indexed(const char *s)
{
    u8 ind = 0, rr, n, force = 0;
    const char *c;
    char tmp[LINEMAX];
    int v;
    u8 post;
    u8 len;
    u16 i, l;
    if (*s == '[') {
        l = strlen(s);
        if (s[l - 1] != ']') { err("missing ]"); return; }
        for (i = 0; i < l - 2; i++) tmp[i] = s[i + 1];
        tmp[l - 2] = 0;
        s = tmp;
        ind = 1;
    }
    c = findcomma(s);
    if (!c) {
        /* [address] */
        if (!ind) { err("bad indexed operand"); return; }
        v = expr(&s);
        ibyte(0x9F);
        iword(v);
        if (xrel) irel = ilen - 2;
        return;
    }
    {
        const char *r = c + 1;
        u8 dec = 0, inc = 0;
        while (*r == '-') { dec++; r++; }
        rr = regidx(r, &len);
        if (rr == 255) { err("bad register"); return; }
        r += len;
        while (*r == '+') { inc++; r++; }
        if (dec > 2 || inc > 2 || (dec && inc) || *r) { err("bad indexed operand"); return; }
        if (c == s) {
            /* ,R  ,R+  ,R++  ,-R  ,--R */
            if (rr > 3) { err("bad register"); return; }
            if (inc == 1) post = 0x80;
            else if (inc == 2) post = 0x81;
            else if (dec == 1) post = 0x82;
            else if (dec == 2) post = 0x83;
            else post = 0x84;
            if (ind && (post == 0x80 || post == 0x82)) { err("indirect auto-increment by one"); return; }
            ibyte((u8)(post | (rr << 5) | (ind ? 0x10 : 0)));
            return;
        }
        if (dec || inc) { err("bad indexed operand"); return; }
        /* A,R B,R D,R */
        if (c - s == 1 && (ucase(s[0]) == 'A' || ucase(s[0]) == 'B' || ucase(s[0]) == 'D') && rr <= 3) {
            post = ucase(s[0]) == 'A' ? 0x86 : ucase(s[0]) == 'B' ? 0x85 : 0x8B;
            ibyte((u8)(post | (rr << 5) | (ind ? 0x10 : 0)));
            return;
        }
        /* n,R  n,PC  n,PCR */
        {
            const char *e = s;
            n = (u8)(c - s);
            if (*e == '<') { force = 8; e++; }
            else if (*e == '>') { force = 16; e++; }
            for (i = 0; i < n - (e - s); i++) tmp[i] = e[i];
            tmp[i] = 0;
            e = tmp;
            v = expr(&e);
            skipws(&e);
            if (*e) { err("bad indexed operand"); return; }
            if (rr == 5) {
                /* the offset from the end of the instruction to the label */
                u8 base = (u8)(ilen + 2);        /* op bytes + post byte + 8 bit offset */
                int o;
                if (!xrel && pass == 2 && !xundef) { err("pcr needs a label"); }
                if (force != 16 && !xfwd && !xundef) {
                    o = v - (pc + base);
                    if (o >= -128 && o <= 127) { ibyte((u8)(0x8C | (ind ? 0x10 : 0))); ibyte((u8)o); return; }
                }
                if (force == 8) { err("pcr offset too far"); return; }
                o = v - (pc + ilen + 3);
                ibyte((u8)(0x8D | (ind ? 0x10 : 0)));
                iword(o);
                return;
            }
            if (rr == 4) {
                nonrel("an address as offset of PC");
                if (force != 16 && !xfwd && v >= -128 && v <= 127) { ibyte((u8)(0x8C | (ind ? 0x10 : 0))); ibyte((u8)v); return; }
                ibyte((u8)(0x8D | (ind ? 0x10 : 0)));
                iword(v);
                return;
            }
            if (xrel) {
                ibyte((u8)(0x89 | (rr << 5) | (ind ? 0x10 : 0)));
                iword(v);
                irel = ilen - 2;
                return;
            }
            if (!force && !ind && !xfwd && v >= -16 && v <= 15) { ibyte((u8)((rr << 5) | (v & 0x1F))); return; }
            if (force != 16 && !xfwd && v >= -128 && v <= 127) { ibyte((u8)(0x88 | (rr << 5) | (ind ? 0x10 : 0))); ibyte((u8)v); return; }
            if (force == 8 && !xfwd) { err("offset too big"); return; }
            ibyte((u8)(0x89 | (rr << 5) | (ind ? 0x10 : 0)));
            iword(v);
        }
    }
}

/* register code for TFR and EXG, and for the PSH/PUL lists */
static int regcode(const char *s, u8 *len)
{
    int a = ucase(s[0]), b = ucase(s[1]);
    u8 l = 0;
    int r = -1;
    if (a == 'D' && (b == 'P')) { r = 0xB; l = 2; }
    else if (a == 'C' && b == 'C') { r = 0xA; l = 2; }
    else if (a == 'P' && b == 'C') { r = 5; l = 2; }
    else if (a == 'D') { r = 0; l = 1; }
    else if (a == 'X') { r = 1; l = 1; }
    else if (a == 'Y') { r = 2; l = 1; }
    else if (a == 'U') { r = 3; l = 1; }
    else if (a == 'S') { r = 4; l = 1; }
    else if (a == 'A') { r = 8; l = 1; }
    else if (a == 'B') { r = 9; l = 1; }
    if (l && isnamec(s[l], 0)) r = -1;
    *len = l;
    return r;
}

static void stacklist(const char *s, u8 isu)
{
    u8 mask = 0, len;
    for (;;) {
        int r;
        u8 bit;
        skipws(&s);
        r = regcode(s, &len);
        if (r < 0) { err("bad register in the list"); return; }
        s += len;
        switch (r) {
        case 0xA: bit = 0x01; break;
        case 8: bit = 0x02; break;
        case 9: bit = 0x04; break;
        case 0xB: bit = 0x08; break;
        case 1: bit = 0x10; break;
        case 2: bit = 0x20; break;
        case 3: bit = 0x40; break;
        case 4: bit = 0x40; break;
        case 5: bit = 0x80; break;
        default: bit = 0x06; break;     /* D */
        }
        if (r == 4 && !isu) { err("S in a list for S"); return; }
        if (r == 3 && isu) { err("U in a list for U"); return; }
        if (r == 4) bit = 0x40;
        if (isu && r == 4) bit = 0x40;
        mask |= bit;
        skipws(&s);
        if (*s == ',') { s++; continue; }
        break;
    }
    ibyte(mask);
}

static void regpair(const char *s)
{
    u8 len;
    int a, b;
    skipws(&s);
    a = regcode(s, &len);
    s += len;
    if (a < 0 || *s != ',') { err("bad register pair"); return; }
    s++;
    skipws(&s);
    b = regcode(s, &len);
    if (b < 0) { err("bad register pair"); return; }
    if ((a < 8) != (b < 8) && !(a == 0xA || b == 0xA)) { }
    ibyte((u8)((a << 4) | b));
}

/* the operand of a machine instruction */
static void instruction(const op_t *o, const char *s)
{
    u8 type = o->type;
    int v;
    ilen = 0;
    irel = -1;
    skipws(&s);
    switch (type) {
    case T_INH:
        iop(o->code);
        return;
    case T_STK:
        iop(o->code);
        stacklist(s, o->code >= 0x36);
        return;
    case T_RR:
        iop(o->code);
        regpair(s);
        return;
    case T_IMM8:
        iop(o->code);
        if (*s == '#') s++;
        v = expr(&s);
        nonrel("an address in a byte");
        ibyte((u8)v);
        return;
    case T_BR: case T_LBR: {
        int off;
        v = expr(&s);
        if (type == T_BR) {
            iop(o->code);
            off = v - (pc + 2);
            if (pass == 2 && !xundef && !xrel) err("a branch needs a label");
            if (pass == 2 && !xundef && (off < -128 || off > 127)) err("branch too far");
            ibyte((u8)off);
        } else {
            iop(o->code);
            off = v - (pc + (o->code > 255 ? 4 : 3));
            if (pass == 2 && !xundef && !xrel) err("a branch needs a label");
            iword(off);
        }
        return;
    }
    case T_LEA:
        iop(o->code);
        if (!findcomma(s) && *s != '[') { err("LEA needs an indexed operand"); return; }
        indexed(s);
        return;
    default:
        break;
    }
    /* memory reference, accumulator, store */
    {
        u16 imm = o->code;
        u16 dir, idx, ext;
        if (type == T_MEM) { dir = imm; idx = imm + 0x60; ext = imm + 0x70; }
        else if (type == T_ST8 || type == T_ST16) { dir = imm; idx = imm + 0x10; ext = imm + 0x20; }
        else { dir = imm + 0x10; idx = imm + 0x20; ext = imm + 0x30; }
        if (*s == '#') {
            if (type == T_MEM || type == T_ST8 || type == T_ST16) { err("immediate not allowed"); return; }
            s++;
            v = expr(&s);
            iop(imm);
            if (type == T_ACC16) {
                iword(v);
                if (xrel) irel = ilen - 2;
            } else {
                nonrel("an address in a byte");
                ibyte((u8)v);
            }
        } else if (findcomma(s) || *s == '[') {
            iop(idx);
            indexed(s);
        } else if (*s == '<') {
            s++;
            v = expr(&s);
            nonrel("an address in direct addressing");
            iop(dir);
            ibyte((u8)v);
        } else {
            if (*s == '>') s++;
            v = expr(&s);
            iop(ext);
            iword(v);
            if (xrel) irel = ilen - 2;
        }
    }
}

/* ---------------------------------------------------------------- directives */
static void putrel(void)
{
    /* the instruction in ib has an address with its high byte at ib[irel] */
    if (pass == 2 && irel >= 0 && !raw) {
        if (nrel < NREL) relpos[nrel++] = (u16)(opos + irel);
        else err("too many addresses to relocate");
    }
}

static void emitins(void)
{
    u8 i;
    if (inbss) { err("an instruction in the bss"); return; }
    putrel();
    for (i = 0; i < ilen; i++) emit(ib[i]);
    pc += ilen;
}

static void defsym(const char *name, int val, u8 rel, u8 bss, u8 abs)
{
    int k = lookup(name, 1);
    sym_t *y;
    if (k < 0) { err("too many names"); return; }
    y = &syms[k];
    if (pass == 1 && (y->flags & F_DEF)) { err("name defined twice"); return; }
    if (pass == 2 && (y->flags & F_DEF) && !abs && y->val != val && !(y->flags & (16 | 32))) {
        err("a name has another value in the second pass (a forward reference decided a length?)");
        return;
    }
    y->val = val;
    y->flags = F_DEF | (rel ? F_REL : 0) | (bss ? F_BSS : 0) | (abs ? 8 : 0);
    y->defline = srcline;
}

/* a string with a delimiter: fcc "text", fcs "text" */
static void putstring(const char *s, u8 hi)
{
    char d = *s++;
    const char *e = s;
    u16 n;
    while (*e && *e != d) e++;
    if (*e != d) { err("a string without the closing delimiter"); return; }
    for (n = 0; s + n < e; n++) {
        u8 b = (u8)s[n];
        if (hi && s + n + 1 == e) b |= 0x80;
        emit(b);
    }
    pc += (u16)(e - s);
}

static void listdir(u8 which, const char *s)
{
    int v;
    ilen = 0; irel = -1;
    for (;;) {
        skipws(&s);
        if (which == 0) {           /* fcb */
            v = expr(&s);
            nonrel("an address in a byte");
            emit((u8)v);
            pc += 1;
        } else {                    /* fdb */
            v = expr(&s);
            if (xrel && pass == 2 && !raw && opos >= 16) {
                if (nrel < NREL) relpos[nrel++] = (u16)opos;
                else err("too many addresses to relocate");
            }
            emit((u8)((u16)v >> 8));
            emit((u8)v);
            pc += 2;
        }
        skipws(&s);
        if (*s == ',') { s++; continue; }
        if (*s) err("junk after the operand");
        break;
    }
}

static void reserve(const char *s, u8 zero)
{
    int n = expr(&s);
    (void)zero;
    if (xundef && pass == 2) err("rmb needs a number that is known");
    if (xrel) err("rmb needs a number");
    if (n < 0) { err("negative size"); return; }
    if (inbss) {
        bpc += n;
        if (bpc > bssmax) bssmax = bpc;
    } else {
        int i;
        for (i = 0; i < n; i++) emit(0);
        pc += n;
    }
}

/* ---------------------------------------------------------------- source: the start code, the runtime, the user's file */
#include "as_text.h"

static u16 sline;
static char **srctext;     /* the text that is read (start code, runtime) */
static char fbuf[256];
static u16 fpos, fcnt;

static int getline_(char *dst)
{
    u16 n = 0;
    if (srcmode == 0 || srcmode == 2) {
        const char *t = srctext[sline];
        if (!t) return 0;
        sline++;
        while (*t && n < LINEMAX) dst[n++] = *t++;
        dst[n] = 0;
        return 1;
    }
    uline++;
    for (;;) {
        char c;
        if (fpos == fcnt) {
            int r = rd(fbuf, sizeof(fbuf));
            fpos = 0;
            fcnt = r > 0 ? (u16)r : 0;
            if (!fcnt) {
                if (n == 0) { uline--; return 0; }
                break;
            }
        }
        c = fbuf[fpos++];
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < LINEMAX) dst[n++] = c;
    }
    dst[n] = 0;
    return 1;
}

static void assemble_line(char *l)
{
    char *p = l;
    char label[NAMEMAX + 1];
    char mnem[8];
    char *oper;
    u8 haslabel = 0, i;
    const op_t *o;

    lineno++;
    srcline = lineno;
    if (*p == 0 || *p == '*' || *p == ';') return;
    /* the label */
    if (*p != ' ' && *p != '\t') {
        char *b = p;
        if (!isnamec(*p, 1)) { err("bad label"); return; }
        while (isnamec(*p, 0)) p++;
        if (*b != '@') scope++;
        mkname(label, b, (u8)(p - b));
        if (*p == ':') p++;
        haslabel = 1;
    }
    skipws((const char **)&p);
    for (i = 0; *p && *p != ' ' && *p != '\t' && i < 7; i++) mnem[i] = (char)ucase(*p++);
    mnem[i] = 0;
    while (*p && *p != ' ' && *p != '\t') p++;
    skipws((const char **)&p);
    oper = p;
    if (*oper == ';') *oper = 0;
    /* the operand ends at a blank outside of quotes (a delimiter for fcc/fcs) */
    {
        u8 str = 0;
        char delim = 0;
        for (; *p; p++) {
            if (str) { if (*p == delim) str = 0; continue; }
            if ((*p == '"') || ((mnem[0] == 'F' && mnem[1] == 'C' && (mnem[2] == 'C' || mnem[2] == 'S')) && p == oper)) { str = 1; delim = *p; continue; }
            if (*p == '\'' && p[1]) { p++; if (p[1] == '\'') p++; continue; }
            if (*p == ' ' || *p == '\t') { *p = 0; break; }
        }
        if (str) { err("a string without the closing delimiter"); }
    }
    if (mnem[0] == 0) {
        if (haslabel) {
            if (inbss) defsym(label, bpc, 1, 1, 0);
            else defsym(label, pc, 1, 0, 0);
        }
        return;
    }
    /* directives that define the label themselves */
    if (!strcmp(mnem, "EQU") || !strcmp(mnem, "SET") || !strcmp(mnem, "=")) {
        int v;
        const char *e = oper;
        if (!haslabel) { err("equ without a name"); return; }
        v = expr(&e);
        if (pass == 1 && xundef) { v = 0; }
        {
            int k = lookup(label, 1);
            if (k >= 0 && (syms[k].flags & F_DEF) && !strcmp(mnem, "SET")) syms[k].flags = 0;
        }
        if (xbss && pass == 2) v -= imgend;
        if (pass == 1 && xundef) xrel = 1;      /* a forward reference: most likely an address (an alias of a label) */
        defsym(label, v, xrel, xbss, !xrel && !xbss);
        if (pass == 1 && xundef) { int k = lookup(label, 0); if (k >= 0) syms[k].flags |= 32; }     /* a forward reference: the value is known in pass 2 */
        if (!strcmp(mnem, "SET")) { int k = lookup(label, 0); if (k >= 0) syms[k].flags |= 16; }
        return;
    }
    if (haslabel) {
        if (inbss) defsym(label, bpc, 1, 1, 0);
        else defsym(label, pc, 1, 0, 0);
    }
    if (!strcmp(mnem, "FCB") || !strcmp(mnem, "BYTE") || !strcmp(mnem, ".BYTE")) { if (inbss) err("data in the bss"); else listdir(0, oper); return; }
    if (!strcmp(mnem, "FDB") || !strcmp(mnem, "WORD") || !strcmp(mnem, ".WORD")) { if (inbss) err("data in the bss"); else listdir(1, oper); return; }
    if (!strcmp(mnem, "FCC")) { if (inbss) err("data in the bss"); else putstring(oper, 0); return; }
    if (!strcmp(mnem, "FCS")) { if (inbss) err("data in the bss"); else putstring(oper, 1); return; }
    if (!strcmp(mnem, "RMB") || !strcmp(mnem, "ZMB") || !strcmp(mnem, "BLKB")) { reserve(oper, 1); return; }
    if (!strcmp(mnem, "BSS")) { inbss = 1; return; }
    if (!strcmp(mnem, "CODE") || !strcmp(mnem, "TEXT")) { inbss = 0; return; }
    if (!strcmp(mnem, "STACK")) {
        const char *e = oper;
        int v = expr(&e);
        if (pass == 1) stksz = v;
        return;
    }
    if (!strcmp(mnem, "RUNTIME")) { userrt = 1; return; }
    if (!strcmp(mnem, "END") || !strcmp(mnem, "OPT") || !strcmp(mnem, "SETDP") || !strcmp(mnem, "NAME")) return;
    if (!strcmp(mnem, "ORG") || !strcmp(mnem, "INCLUDE") || !strcmp(mnem, "IMPORT") || !strcmp(mnem, "EXPORT") || !strcmp(mnem, "SECTION")) {
        err("directive not supported");
        return;
    }
    for (o = optab; o->name; o++)
        if (!strcmp(o->name, mnem)) break;
    if (!o->name) { err("unknown instruction"); return; }
    instruction(o, oper);
    emitins();
}

static void run_pass(void)
{
    char l[LINEMAX + 2];
    pc = 0; bpc = 0; inbss = 0; opos = 0; lineno = 0; scope = 0; nrel = 0; uline = 0;
    srcmode = raw ? 1 : 0;
    sline = 0;
    srctext = as_start;
    fpos = fcnt = 0;
#ifdef AS_HOST
    fseek(fin, 0, SEEK_SET);
#else
    seek(fin, 0, 0, 0, NULL, NULL);
#endif
    for (;;) {
        if (srcmode == 3) break;
        if (!getline_(l)) {
            if (srcmode == 0) { srcmode = 1; curfile = curfile; continue; }
            if (srcmode == 1 && userrt && !raw) { srcmode = 2; sline = 0; srctext = as_runtime; continue; }
            srcmode = 3;
            break;
        }
        assemble_line(l);
        inbss = inbss;
    }
    inbss = 0;
}

/* ---------------------------------------------------------------- main */
static void print_symbols(void)
{
    u16 i;
    for (i = 0; i < nsym; i++) {
        if (syms[i].name[0] == '@' || (syms[i].name[0] == '_' && syms[i].name[1] == '_')) continue;
        LOG("%04x %c %s\n", (u16)(syms[i].val + ((syms[i].flags & F_BSS) ? imgend : 0)), (syms[i].flags & F_REL) ? 'A' : ' ', syms[i].name);
    }
}

static char outname[64];

/* "; program arguments ...": run the program in place of this one when the assembly is done (the stages of the C compiler, docs/IR.md) */
static int run_tail(int argc, char **argv, int at)
{
#ifdef AS_HOST
    (void)argc; (void)argv; (void)at;
    return 0;
#else
    char blk[ARGMAX];
    u16 len;
    int i;
    if (at + 1 >= argc) return 0;
    strcpy(blk, "/BIN/");
    strcat(blk, argv[at + 1]);
    strcat(blk, ".MX");
    len = (u16)strlen(blk) + 1;
    for (i = at + 2; i < argc; i++) {
        u16 l = (u16)strlen(argv[i]) + 1;
        if (len + l + 1 > ARGMAX - SB_ARGS) { puts("as: the command line of the next stage is too long"); return 1; }
        memcpy(blk + len, argv[i], l);
        len += l;
    }
    blk[len++] = 0;
    return exec(blk, len);
#endif
}

int main(int argc, char **argv)
{
    int i, tail = argc;
    const char *src = NULL, *out = NULL;
    for (i = 1; i < argc; i++) if (argv[i][0] == ';' && argv[i][1] == 0) { tail = i; break; }
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-r")) raw = 1;
        else if (!strcmp(argv[i], "-s")) showsyms = 1;
        else if (!strcmp(argv[i], "-o") && i + 1 < tail) out = argv[++i];
        else if (argv[i][0] == '-') { LOG("as: unknown option %s\n", argv[i]); return 2; }
        else src = argv[i];
    }
    if (!src) { LOG("usage: as [-r] [-s] [-o out] file.s\n"); return 2; }
    if (!out) {
        u16 n = strlen(src);
        if (n > 59) n = 59;
        memcpy(outname, src, n);
        outname[n] = 0;
        if (n > 2 && outname[n - 2] == '.' && ucase(outname[n - 1]) == 'S') { outname[n - 2] = 0; }
        strcat(outname, raw ? ".bin" : ".mx");
        out = outname;
    }
    curfile = src;
#ifdef AS_HOST
    fin = fopen(src, "rb");
    if (!fin) { LOG("as: cannot open %s\n", src); return 2; }
#else
    fin = open(src, FO_READ);
    if (fin < 0) { LOG("as: cannot open %s\n", src); return 2; }
#endif
    pass = 1;
    run_pass();
    imgend = pc;
    if (!raw) {
        int k;
        /* what the start code needs to know */
        k = lookup("__size", 1); syms[k].val = imgend; syms[k].flags = F_DEF | 8; syms[k].defline = 0;
        k = lookup("__bss", 1); syms[k].val = bssmax; syms[k].flags = F_DEF | 8; syms[k].defline = 0;
        k = lookup("__end", 1); syms[k].val = imgend; syms[k].flags = F_DEF | F_REL; syms[k].defline = 0;
        k = lookup("__stack", 1); syms[k].val = stksz; syms[k].flags = F_DEF | 8; syms[k].defline = 0;
    }
    noout = e1 != 0;                    /* errors: the second pass only looks for more of them */
    if (!noout) {
#ifdef AS_HOST
        fout = fopen(out, "wb");
        if (!fout) { LOG("as: cannot create %s\n", out); return 2; }
#else
        fout = open(out, FO_WRITE | FO_CREATE | FO_TRUNC);
        if (fout < 0) { LOG("as: cannot create %s\n", out); return 2; }
#endif
    }
    pass = 2;
    /* the bss addresses were counted from 0 in pass 1; the first pass has the size of the image now */
    run_pass();
    oflush();
    if (!errors && !noout && !raw) {
        /* the relocation table (the format of lib/crt0app.s): the gaps between the places, 255 = skip 254 bytes, 0 = end */
        u16 cur = 0, j;
        u8 tb[64];
        u8 tn = 0;
        for (j = 0; j < nrel; j++) {
            u16 g = relpos[j] - cur;
            while (g > 254) { tb[tn++] = 255; g -= 254; if (tn > 60) { wr(tb, tn); tn = 0; } }
            tb[tn++] = (u8)g;
            cur = relpos[j];
            if (tn > 60) { wr(tb, tn); tn = 0; }
        }
        tb[tn++] = 0;
        wr(tb, tn);
    }
    if (showsyms) print_symbols();
#ifdef AS_HOST
    fclose(fin);
    if (!noout) fclose(fout);
#else
    close(fin);
    if (!noout) close(fout);
#endif
    if (errors || noout) {
        if (!errors) errors = e1;
        LOG("as: %u error(s)\n", errors);
        if (!noout) {
#ifdef AS_HOST
            remove(out);
#else
            unlink(out);
#endif
        }
        return 1;
    }
    LOG("%s: %u bytes, %u relocations, bss %u\n", out, (u16)imgend, nrel, (u16)bssmax);
    return run_tail(argc, argv, tail);
}
