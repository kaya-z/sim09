/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * cc1 - the front end of the C compiler of MK09: preprocessed C in, IR out (docs/IR.md)
 *
 *   cc1 [-o out.ir] file.i
 *
 * Types: char int short unsigned signed void, pointers, arrays, struct, union, enum, typedef, function pointers.  No long, float, bit fields,
 * K&R function definitions, structures passed or returned by value.  Everything is 16 bits: a char is a byte in memory and an int in a value.
 * The code is a stack machine; the arguments of a call are put in the output from right to left, so they are first written to an arena.
 */
#include "ccio.h"

#define TOOL_VERSION "1.6"          /* the version of this program: changed when it changes (-v) */

#define NTAG    24
/* the sizes of the tables: small on MK09 (the whole program must fit in the transient area), big on the PC (the library is compiled there) */
#ifdef CC_HOST
#define NGSYM   400
#define NLSYM   100
#define NTY     500
#define NMEM    400
#define ARENA   2500
#else
#define NGSYM   100
#define NLSYM   48
#define NTY     120
#define NMEM    72
#define ARENA   1300
#endif
#define NAMEMAX 15
#define STRMAX  160
#define NCASE   60
#define NLAB    16

/* ---------------------------------------------------------------- tokens */
enum {
    T_EOF = 0, T_NUM = 128, T_ID, T_STR,
    K_VOID, K_CHAR, K_INT, K_SHORT, K_LONG, K_UNSIGNED, K_SIGNED, K_FLOAT, K_DOUBLE, K_STRUCT, K_UNION, K_ENUM, K_TYPEDEF, K_STATIC, K_EXTERN, K_CONST,
    K_VOLATILE, K_REGISTER, K_AUTO, K_IF, K_ELSE, K_WHILE, K_DO, K_FOR, K_SWITCH, K_CASE, K_DEFAULT, K_BREAK, K_CONTINUE, K_RETURN,
    K_GOTO, K_SIZEOF,
    T_INC, T_DEC, T_SHL, T_SHR, T_LE, T_GE, T_EQ, T_NE, T_ANDAND, T_OROR, T_ARROW, T_ADDEQ, T_SUBEQ, T_MULEQ, T_DIVEQ, T_MODEQ,
    T_ANDEQ, T_OREQ, T_XOREQ, T_SHLEQ, T_SHREQ, T_ELLIP
};

static char *keywords[] = {
    "void", "char", "int", "short", "long", "unsigned", "signed", "float", "double", "struct", "union", "enum", "typedef", "static", "extern", "const",
    "volatile", "register", "auto", "if", "else", "while", "do", "for", "switch", "case", "default", "break", "continue", "return",
    "goto", "sizeof", 0
};

static rfile in;
static wfile ir, df, sf;       /* the code, the data (global and static variables), the strings */
static u8 errors;
static char fname[120];
static u16 line = 1;
static int ch;
static u16 tok;
static int tokv;
static u8 tokun;
static u16 tokvh;            /* the high 16 bits of a number: a constant above 65535 or with an L is a long */
static u8 toklong;
static char toks[STRMAX + 2];
static u16 toklen;
static u8 haspeek;
static u16 ptok;
static int ptokv;
static u8 ptokun;
static u16 ptokvh;
static u8 ptoklong;
static char ptoks[STRMAX + 2];
static u16 ptoklen;

/* With MSGS (the build for MK09, see tools/msgsplit.py) a message is a number: line N of /LIB/CC1.MSG */
#ifdef MSGS
typedef u8 msgt;
#else
typedef const char *msgt;
#endif

static void err(msgt msg)
{
    errors++;
    w_flush(&ir);
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", fname, line, msg);
#elif defined(MSGS)
    {
        rfile r;
        int c;
        u8 n = 1;
        char nb[7];
        putstr(fname); putchar(':'); utoa_(line, nb, 10); putstr(nb); putstr(": ");
        if (!r_open(&r, "/LIB/CC1.MSG")) {
            while (n < msg && (c = r_getc(&r)) >= 0) if (c == '\n') n++;
            while ((c = r_getc(&r)) >= 0 && c != '\n') putchar(c);
            r_close(&r);
        } else { putstr("error "); utoa_(msg, nb, 10); putstr(nb); }
        putchar('\n');
    }
#else
    printf("%s:%u: %s\n", fname, line, msg);
#endif
}

static int cc_atoi(const char *p)
{
    int v = 0;
    u8 neg = 0;
    if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
    while (*p >= '0' && *p <= '9') v = v * 10 + (*p++ - '0');
    return neg ? -v : v;
}

/* the name "S<n>" (a string) or "V<n>" (a static variable) */
static void sname(char *d, char pre, u16 n)
{
    char t[6];
    u8 k = 0, m = 0;
    d[k++] = pre;
    do { t[m++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (m) d[k++] = t[--m];
    d[k] = 0;
}

static void nextch(void) { ch = r_getc(&in); }
static int isidc(int c, u8 first)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return !first && c >= '0' && c <= '9';
}

static int escape(void)
{
    int v;
    nextch();       /* behind the backslash */
    switch (ch) {
    case 'n': v = 10; break;
    case 't': v = 9; break;
    case 'r': v = 13; break;
    case 'a': v = 7; break;
    case 'b': v = 8; break;
    case 'f': v = 12; break;
    case 'v': v = 11; break;
    case 'x': {
        v = 0;
        nextch();
        while ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            v = v * 16 + (ch <= '9' ? ch - '0' : (ch | 32) - 'a' + 10);
            nextch();
        }
        return v & 255;
    }
    default:
        if (ch >= '0' && ch <= '7') {
            u8 n = 0;
            v = 0;
            while (n < 3 && ch >= '0' && ch <= '7') { v = v * 8 + ch - '0'; nextch(); n++; }
            return v & 255;
        }
        v = ch;
        break;
    }
    nextch();
    return v;
}

/* # N "file": the next line is line N of the file (ch is the #) */
static void read_marker(void)
{
    int n = 0;
    u8 i = 0;
    nextch();
    while (ch == ' ') nextch();
    while (ch >= '0' && ch <= '9') { n = n * 10 + ch - '0'; nextch(); }
    while (ch == ' ') nextch();
    if (ch == '"') {
        nextch();
        while (ch >= 0 && ch != '"' && ch != '\n') { if (i < sizeof(fname) - 1) fname[i++] = (char)ch; nextch(); }
        fname[i] = 0;
    }
    while (ch >= 0 && ch != '\n') nextch();
    line = (u16)(n - 1);
}

static u16 nh, nl;               /* a number: 32 bits */

static void num_step(u8 base, u8 d)       /* (nh:nl) = (nh:nl) * base + d */
{
    u16 ah = 0, al = 0, t;
    u8 i;
    for (i = 0; i < base; i++) {
        t = al + nl;
        ah += nh + (t < al);
        al = t;
    }
    t = al + d;
    ah += (t < al);
    nh = ah;
    nl = t;
}

static void lex(void)
{
    for (;;) {
        while (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\f') nextch();
        if (ch == '\n') {
            line++;
            nextch();
            if (ch == '#') read_marker();
            continue;
        }
        break;
    }
    tokun = 0; toklong = 0; tokvh = 0;
    if (ch < 0) { tok = T_EOF; return; }
    if (isidc(ch, 1)) {
        u8 n = 0, k;
        while (isidc(ch, 0)) { if (n < NAMEMAX) toks[n++] = (char)ch; nextch(); }
        toks[n] = 0;
        tok = T_ID;
        for (k = 0; keywords[k]; k++)
            if (!strcmp(keywords[k], toks)) { tok = (u16)(K_VOID + k); break; }
        return;
    }
    if (ch >= '0' && ch <= '9') {
        u8 base = 10;
        nh = nl = 0;
        tok = T_NUM;
        if (ch == '0') {
            nextch();
            if (ch == 'x' || ch == 'X') {
                base = 16;
                nextch();
                while ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
                    num_step(16, (u8)(ch <= '9' ? ch - '0' : (ch | 32) - 'a' + 10));
                    nextch();
                }
            } else {
                while (ch >= '0' && ch <= '7') { num_step(8, (u8)(ch - '0')); nextch(); }
            }
        } else {
            while (ch >= '0' && ch <= '9') { num_step(10, (u8)(ch - '0')); nextch(); }
        }
        (void)base;
        while (ch == 'u' || ch == 'U' || ch == 'l' || ch == 'L') { if (ch == 'u' || ch == 'U') tokun = 1; else toklong = 1; nextch(); }
        tokv = (int)nl;
        tokvh = nh;
        return;
    }
    if (ch == '@') {                            /* a float constant: cpp made it into @ and 8 hex digits (the bits) */
        nh = nl = 0;
        nextch();
        while ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')) {
            num_step(16, (u8)(ch <= '9' ? ch - '0' : (ch | 32) - 'a' + 10));
            nextch();
        }
        tok = T_NUM; toklong = 2; tokv = (int)nl; tokvh = nh;
        return;
    }
    if (ch == '\'') {
        nextch();
        tok = T_NUM;
        if (ch == '\\') tokv = escape();
        else { tokv = (int)(s8)ch; nextch(); }
        if (ch == '\'') nextch(); else err("bad character constant");
        return;
    }
    if (ch == '"') {
        toklen = 0;
        tok = T_STR;
        for (;;) {
            nextch();
            while (ch >= 0 && ch != '"' && ch != '\n') {
                int c = ch;
                if (ch == '\\') c = escape(); else nextch();
                if (toklen < STRMAX) toks[toklen++] = (char)c;
            }
            if (ch != '"') { err("a string without the closing quote"); break; }
            nextch();
            while (ch == ' ' || ch == '\t' || ch == '\n') { if (ch == '\n') line++; nextch(); }
            if (ch != '"') break;               /* adjacent strings are one string */
        }
        toks[toklen] = 0;
        return;
    }
    /* operators */
    {
        int c = ch;
        nextch();
        tok = (u16)c;
        switch (c) {
        case '+': if (ch == '+') { tok = T_INC; nextch(); } else if (ch == '=') { tok = T_ADDEQ; nextch(); } break;
        case '-': if (ch == '-') { tok = T_DEC; nextch(); } else if (ch == '=') { tok = T_SUBEQ; nextch(); } else if (ch == '>') { tok = T_ARROW; nextch(); } break;
        case '*': if (ch == '=') { tok = T_MULEQ; nextch(); } break;
        case '/': if (ch == '=') { tok = T_DIVEQ; nextch(); } break;
        case '%': if (ch == '=') { tok = T_MODEQ; nextch(); } break;
        case '&': if (ch == '&') { tok = T_ANDAND; nextch(); } else if (ch == '=') { tok = T_ANDEQ; nextch(); } break;
        case '|': if (ch == '|') { tok = T_OROR; nextch(); } else if (ch == '=') { tok = T_OREQ; nextch(); } break;
        case '^': if (ch == '=') { tok = T_XOREQ; nextch(); } break;
        case '=': if (ch == '=') { tok = T_EQ; nextch(); } break;
        case '!': if (ch == '=') { tok = T_NE; nextch(); } break;
        case '<':
            if (ch == '<') { nextch(); if (ch == '=') { tok = T_SHLEQ; nextch(); } else tok = T_SHL; }
            else if (ch == '=') { tok = T_LE; nextch(); }
            break;
        case '>':
            if (ch == '>') { nextch(); if (ch == '=') { tok = T_SHREQ; nextch(); } else tok = T_SHR; }
            else if (ch == '=') { tok = T_GE; nextch(); }
            break;
        case '.':
            if (ch == '.') { nextch(); if (ch == '.') { nextch(); tok = T_ELLIP; } }
            break;
        default: break;
        }
    }
}

static void next(void)
{
    if (haspeek) {
        tok = ptok; tokv = ptokv; tokun = ptokun; toklen = ptoklen; tokvh = ptokvh; toklong = ptoklong;
        memcpy(toks, ptoks, STRMAX + 1);
        haspeek = 0;
        return;
    }
    lex();
}

/* the token behind the current one */
static u16 peek(void)
{
    u16 st = tok;
    int sv = tokv;
    u8 su = tokun;
    u16 sl = toklen;
    u16 svh = tokvh;
    u8 slg = toklong;
    char ss[STRMAX + 2];
    if (haspeek) return ptok;
    memcpy(ss, toks, STRMAX + 1);
    lex();
    ptok = tok; ptokv = tokv; ptokun = tokun; ptoklen = toklen; ptokvh = tokvh; ptoklong = toklong;
    memcpy(ptoks, toks, STRMAX + 1);
    haspeek = 1;
    tok = st; tokv = sv; tokun = su; toklen = sl; tokvh = svh; toklong = slg;
    memcpy(toks, ss, STRMAX + 1);
    return ptok;
}

static void expect(u16 t, msgt what)
{
    if (tok != t) { err(what); return; }
    next();
}

/* ---------------------------------------------------------------- types */
enum { TK_VOID, TK_CHAR, TK_INT, TK_PTR, TK_ARR, TK_STRUCT, TK_UNION, TK_FUNC, TK_LONG, TK_FLOAT };
typedef struct ty {
    u8 k, un;
    u16 size;
    u16 base;       /* pointer, array, function: the type pointed to, the element, the result */
    u16 n;          /* array: elements;  struct/union: the first member (index + 1);  function: unused */
} ty_t;
static ty_t tys[NTY];
static u16 nty = 8;
#define T_VOIDT 0
#define T_CHART 1
#define T_UCHART 2
#define T_INTT  3
#define T_UINTT 4
#define T_LONGT 5
#define T_ULONGT 6
#define T_FLOATT 7
static u8 float_seen;          /* the program has a float: printf gets the means to print it (pointer __fmtfp) */

typedef struct mem {
    char name[NAMEMAX + 1];
    u16 ty;
    u16 off;
    u16 next;       /* index + 1 */
} mem_t;
static mem_t mems[NMEM];
static u16 nmem;

typedef struct tag {
    char name[NAMEMAX + 1];
    u16 ty;
} tag_t;
static tag_t tags[NTAG];
static u8 ntag;

static u16 newty(u8 k, u16 size, u16 base)
{
    if (nty >= NTY) { err("too many types"); return 0; }
    tys[nty].k = k; tys[nty].un = 0; tys[nty].size = size; tys[nty].base = base; tys[nty].n = 0;
    return nty++;
}

static u16 ptr_to(u16 t)
{
    u16 i;
    for (i = 8; i < nty; i++) if (tys[i].k == TK_PTR && tys[i].base == t) return i;
    return newty(TK_PTR, 2, t);
}

static u16 arr_of(u16 t, u16 n)
{
    u16 r = newty(TK_ARR, (u16)(tys[t].size * n), t);
    tys[r].n = n;
    return r;
}

static u16 func_of(u16 t) { return newty(TK_FUNC, 0, t); }
static u8 is_ptr(u16 t) { return tys[t].k == TK_PTR || tys[t].k == TK_ARR; }
static u8 is_int(u16 t) { return tys[t].k == TK_CHAR || tys[t].k == TK_INT; }
static u8 is_long(u16 t) { return tys[t].k == TK_LONG; }
static u8 is_flt(u16 t) { return tys[t].k == TK_FLOAT; }
static u8 is_wide(u16 t) { return tys[t].k >= TK_LONG; }       /* long or float: an object of 4 bytes */
static u8 is_agg(u16 t) { return tys[t].k == TK_STRUCT || tys[t].k == TK_UNION; }
static u8 is_unsigned(u16 t) { return tys[t].k == TK_PTR || tys[t].k == TK_ARR || tys[t].un; }

/* ---------------------------------------------------------------- symbols */
enum { C_GLOBAL, C_LOCAL, C_PARAM, C_STATIC, C_TYPEDEF, C_ENUM };
typedef struct sym {
    char name[NAMEMAX + 1];
    u8 cls;
    u8 flags;       /* globals: 1 defined, 2 used, 4 function, 8 static */
    u16 ty;
    int val;        /* local: the offset in the frame; param: its number; static: the number of its label; enum: its value */
} sym_t;
static sym_t gsyms[NGSYM];
static u16 ngsym;
static sym_t lsyms[NLSYM];
static u16 nlsym;

static sym_t *find_sym(const char *n)
{
    u16 i = nlsym;
    while (i) { i--; if (!strcmp(lsyms[i].name, n)) return &lsyms[i]; }
    for (i = 0; i < ngsym; i++) if (!strcmp(gsyms[i].name, n)) return &gsyms[i];
    return NULL;
}

static sym_t *find_global(const char *n)
{
    u16 i;
    for (i = 0; i < ngsym; i++) if (!strcmp(gsyms[i].name, n)) return &gsyms[i];
    return NULL;
}

static sym_t *new_global(const char *n, u8 cls, u16 ty)
{
    sym_t *s;
    if (ngsym >= NGSYM) { err("too many global names"); return &gsyms[0]; }
    s = &gsyms[ngsym++];
    strcpy(s->name, n);
    s->cls = cls; s->flags = 0; s->ty = ty; s->val = 0;
    return s;
}

static sym_t *new_local(const char *n, u8 cls, u16 ty, int val)
{
    sym_t *s;
    if (nlsym >= NLSYM) { err("too many local names"); return &lsyms[0]; }
    s = &lsyms[nlsym++];
    strcpy(s->name, n);
    s->cls = cls; s->flags = 0; s->ty = ty; s->val = val;
    return s;
}

/* ---------------------------------------------------------------- output of the IR */
static char arena[ARENA];
static u16 alen;
static u8 to_arena;
static u16 nlab;
static u16 nstat;

static void put(const char *s)
{
    if (to_arena) {
        while (*s) { if (alen < ARENA - 1) arena[alen++] = *s; else { err("expression too complex"); return; } s++; }
    } else w_puts(&ir, s);
}

static void putn(int v)
{
    char t[8];
    u8 n = 0;
    u16 u;
    if (v < 0) { put("-"); u = (u16)(-v); } else u = (u16)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) { char c[2]; c[0] = t[--n]; c[1] = 0; put(c); }
}

static void ins0(const char *op) { put("\t"); put(op); put("\n"); }
static void ins1(const char *op, int n) { put("\t"); put(op); put(" "); putn(n); put("\n"); }
static void insl(const char *op, u16 l) { put("\t"); put(op); put(" L"); putn(l); put("\n"); }
static void deflabel(u16 l) { put("L"); putn(l); put(":\n"); }
static void insaddr(const char *name, int off)
{
    put("\taddr ");
    put(name);
    if (off) { if (off > 0) put("+"); putn(off); }
    put("\n");
}
static void aliaslabel(u16 a, u16 b) { put("\talias L"); putn(a); put(" L"); putn(b); put("\n"); }

/* ---------------------------------------------------------------- expression state */
static u16 e_t;     /* the type */
static u8 e_lv;     /* the address of the object is on the stack */
static u8 e_c;      /* a constant: e_v, nothing is emitted yet */
static int e_v;
static int e_fn;    /* a function name that is not emitted yet: the index in gsyms + 1 */

static u16 fp, maxfp;          /* the frame: bytes used now, at most */
static u16 fn_ret;             /* the type that the function being compiled returns */
static void parse_expr(void);
static void parse_assign(void);
static void parse_cast(void);
static void statement(void);

static void rvalue(void)
{
    if (e_fn) {
        insaddr(gsyms[e_fn - 1].name, 0);
        gsyms[e_fn - 1].flags |= 2;
        e_t = ptr_to(e_t);
        e_fn = 0;
        e_lv = 0;
        return;
    }
    if (e_lv) {
        u8 k = tys[e_t].k;
        if (k == TK_ARR) { e_t = ptr_to(tys[e_t].base); e_lv = 0; }
        else if (k == TK_FUNC) { e_t = ptr_to(e_t); e_lv = 0; }
        else if (k == TK_STRUCT || k == TK_UNION || k >= TK_LONG) { }
        else {
            if (k == TK_CHAR) { ins0(tys[e_t].un ? "load.ub" : "load.b"); e_t = T_INTT; }
            else ins0("load.w");
            e_lv = 0;
        }
    }
}

static void force(void)
{
    if (e_c) { ins1("const", e_v); e_c = 0; }
}

static void rv(void) { rvalue(); force(); }

/* ---------------------------------------------------------------- constants and operators */
static int fold(u16 op, int a, int b, u8 un, u8 *ok)
{
    u16 ua = (u16)a, ub = (u16)b;
    *ok = 1;
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case '/': if (!b) { *ok = 0; return 0; } return un ? (int)(ua / ub) : a / b;
    case '%': if (!b) { *ok = 0; return 0; } return un ? (int)(ua % ub) : a % b;
    case '&': return a & b;
    case '|': return a | b;
    case '^': return a ^ b;
    case T_SHL: return b >= 16 ? 0 : (int)(u16)(ua << b);
    case T_SHR: if (un) return b >= 16 ? 0 : (int)(ua >> b); return b >= 16 ? (a < 0 ? -1 : 0) : a >> b;
    case T_EQ: return a == b;
    case T_NE: return a != b;
    case '<': return un ? ua < ub : a < b;
    case '>': return un ? ua > ub : a > b;
    case T_LE: return un ? ua <= ub : a <= b;
    case T_GE: return un ? ua >= ub : a >= b;
    }
    *ok = 0;
    return 0;
}

static const char *opname(u16 op, u8 un)
{
    switch (op) {
    case '+': return "add";
    case '-': return "sub";
    case '*': return "mul";
    case '/': return un ? "udiv" : "div";
    case '%': return un ? "umod" : "mod";
    case '&': return "and";
    case '|': return "or";
    case '^': return "xor";
    case T_SHL: return "shl";
    case T_SHR: return un ? "ushr" : "shr";
    case T_EQ: return "eq";
    case T_NE: return "ne";
    case '<': return un ? "ult" : "lt";
    case '>': return un ? "ugt" : "gt";
    case T_LE: return un ? "ule" : "le";
    case T_GE: return un ? "uge" : "ge";
    }
    return "?";
}

static u8 is_cmp(u16 op) { return op == T_EQ || op == T_NE || op == '<' || op == '>' || op == T_LE || op == T_GE; }
static u8 commutes(u16 op) { return op == '+' || op == '*' || op == '&' || op == '|' || op == '^' || op == T_EQ || op == T_NE; }

/* the size of what a pointer points to (1 for void) */
static u16 pointee(u16 t)
{
    u16 b = tys[t].base;
    return tys[b].size ? tys[b].size : 1;
}

/* scale the integer on top of the stack (or e_v when it is a constant) */
static void scale(u16 sz)
{
    if (sz == 1) return;
    if (e_c) { e_v = (int)(e_v * sz); return; }
    ins1("const", sz);
    ins0("mul");
}

/* ---------------------------------------------------------------- long: an object of 4 bytes (hi word first); the operations are calls of the library (clib.c)
 * A long value is always an address on the stack (e_lv = 1): a variable, or a temporary of the frame that holds a result.  The library functions get the
 * addresses:  __ladd(d, b, a) and so on (the stack holds a, b, d: d is the top).  A function returns a long in __lret. */
enum { LH_ITOL, LH_UTOL, LH_ADD, LH_SUB, LH_MUL, LH_DIV, LH_MOD, LH_UDIV, LH_UMOD, LH_AND, LH_OR, LH_XOR, LH_SHL, LH_SHR, LH_USHR, LH_NEG, LH_COM,
       LH_NZ, LH_CMP, LH_UCMP, LH_INC, LH_RET,
       LH_ITOF, LH_UTOF, LH_LTOF, LH_ULTOF, LH_FTOL, LH_FTOI, LH_FADD, LH_FSUB, LH_FMUL, LH_FDIV, LH_FNEG, LH_FNZ, LH_FCMP, LH_FINC, LH_N };
static char *lh_name[LH_N] = { "__itol", "__utol", "__ladd", "__lsub", "__lmul", "__ldiv", "__lmod", "__ludiv", "__lumod", "__land", "__lor", "__lxor",
                               "__lshl", "__lshr", "__lushr", "__lneg", "__lcom", "__lnz", "__lcmp", "__lucmp", "__linc", "__lret",
                               "__itof", "__utof", "__ltof", "__ultof", "__ftol", "__ftoi", "__fadd", "__fsub", "__fmul", "__fdiv", "__fneg", "__fnz", "__fcmp", "__finc" };
static u8 lh_used[LH_N];
static u16 tmp_used;           /* bytes of temporaries of the statement */

static u16 new_temp(void)
{
    u16 off = (u16)(fp + tmp_used);
    tmp_used = (u16)(tmp_used + 4);
    if (fp + tmp_used > maxfp) maxfp = (u16)(fp + tmp_used);
    return off;
}

static void lcall(u8 id, u8 n)
{
    lh_used[id] = 1;
    put("\tcall ");
    put(lh_name[id]);
    put(" ");
    putn(n);
    put("\n");
}

static void wide_to_int(void)          /* the low word of the long whose address is on the stack, or the int of a float */
{
    u8 un = tys[e_t].un;
    if (is_flt(e_t)) { lcall(LH_FTOI, 1); e_t = T_INTT; e_lv = 0; return; }
    ins1("const", 2);
    ins0("add");
    ins0("load.w");
    e_t = un ? T_UINTT : T_INTT;
    e_lv = 0;
}

/* the helper that makes an object of type `to` (long or float) of a value of type `from` (an int in a word, or the address of a long or a float) */
static u8 conv_id(u16 from, u16 to)
{
    u8 un = is_unsigned(from);
    if (is_flt(to)) return is_long(from) ? (tys[from].un ? LH_ULTOF : LH_LTOF) : (un ? LH_UTOF : LH_ITOF);
    return is_flt(from) ? LH_FTOL : (un ? LH_UTOL : LH_ITOL);
}

/* e_* is an rvalue: an object of the kind of tt (long or float) is made of it (in a temporary) if it is not one */
static void to_wide(u16 tt)
{
    u16 t;
    if (tys[e_t].k == tys[tt].k) return;
    force();
    t = new_temp();
    ins1("laddr", t);
    lcall(conv_id(e_t, tt), 2);
    ins0("drop");
    ins1("laddr", t);
    e_t = tt; e_lv = 1; e_c = 0;
}

/* the value for a jump: a long becomes an int (0 or not) */
static void truth(void)
{
    rv();
    if (is_wide(e_t)) { lcall(is_flt(e_t) ? LH_FNZ : LH_NZ, 1); e_t = T_INTT; e_lv = 0; }
}

static u8 long_op_id(u16 op, u8 un)
{
    switch (op) {
    case '+': return LH_ADD;
    case '-': return LH_SUB;
    case '*': return LH_MUL;
    case '/': return un ? LH_UDIV : LH_DIV;
    case '%': return un ? LH_UMOD : LH_MOD;
    case '&': return LH_AND;
    case '|': return LH_OR;
    case '^': return LH_XOR;
    case T_SHL: return LH_SHL;
    }
    return un ? LH_USHR : LH_SHR;
}

static u8 is_cmp(u16 op);
static const char *opname(u16 op, u8 un);

/* op: the operator; the left operand is a constant (lc, lval) or on the stack (lt: its type); the right one is in e_* (rvalue'd); one of them is a long */
static void binary(u16 op, u8 lc, int lval, u16 lt);

/* the helper of an operator for a long or a float (t: the type of the left operand or of the result) */
static u8 wide_op_id(u16 op, u16 t)
{
    if (is_flt(t)) {
        if (op != '+' && op != '-' && op != '*' && op != '/') { err("bad operands for a float"); return LH_FADD; }
        return op == '+' ? LH_FADD : op == '-' ? LH_FSUB : op == '*' ? LH_FMUL : LH_FDIV;
    }
    return long_op_id(op, tys[t].un);
}

static void wide_binary(u16 op, u8 lc, int lval, u16 lt)
{
    u8 shift = (op == T_SHL || op == T_SHR);
    u8 un, fl;
    u16 t, rt = e_t, tt;
    fl = !shift && (is_flt(lt) || is_flt(rt));
    if (shift) {
        if (is_flt(lt) || is_flt(rt)) { err("a shift of a float"); return; }
        if (!is_long(lt)) {            /* an int shifted by a long: the count is an int */
            if (is_long(rt)) wide_to_int();
            binary(op, lc, lval, lt);
            return;
        }
        if (is_long(rt)) wide_to_int();
        force();                       /* the count is on the stack */
        un = tys[lt].un;
        tt = lt;
    } else {
        un = (is_wide(lt) ? tys[lt].un : is_unsigned(lt)) || (is_wide(rt) ? tys[rt].un : is_unsigned(rt));
        tt = fl ? T_FLOATT : un ? T_ULONGT : T_LONGT;
        if (tys[lt].k != tys[tt].k) {      /* the left operand is not of that kind: an int (a constant not emitted yet, or a word under the address of the right one), or a long */
            if (lc) ins1("const", lval); else ins0("swap");
            t = new_temp();
            ins1("laddr", t);
            lcall(conv_id(lt, tt), 2);
            ins0("drop");
            ins1("laddr", t);
            ins0("swap");
        } else if (tys[rt].k != tys[tt].k) {
            to_wide(tt);
        }
    }
    if (is_cmp(op)) {
        lcall(fl ? LH_FCMP : un ? LH_UCMP : LH_CMP, 2);
        ins1("const", 0);
        ins0(opname(op, 0));
        e_t = T_INTT; e_lv = 0; e_c = 0;
        return;
    }
    t = new_temp();
    ins1("laddr", t);
    lcall(wide_op_id(op, tt), 3);
    ins0("drop");
    ins1("laddr", t);
    e_t = tt; e_lv = 1; e_c = 0;
}

/* op: the operator; the left operand is a constant (lc, lval) or on the stack; the right one is in e_* (rvalue'd, not forced) */
static void binary(u16 op, u8 lc, int lval, u16 lt)
{
    u16 rt = e_t;
    u8 rc = e_c;
    int rval = e_v;
    u8 lp = is_ptr(lt), rp = is_ptr(rt);
    u16 res = T_INTT;
    u8 un;
    u8 ok;
    int v;
    if (is_wide(lt) || is_wide(rt)) { wide_binary(op, lc, lval, lt); return; }
    if (is_agg(lt) || is_agg(rt)) { err("a structure in an expression"); return; }
    if (op == '+' && (lp || rp)) {
        if (lp && rp) { err("two pointers added"); return; }
        if (lp) {          /* ptr + int: scale the right operand */
            scale(pointee(lt));
            rval = e_v;
            res = lt;
        } else {           /* int + ptr */
            u16 sz = pointee(rt);
            if (lc) { lval = (int)(lval * sz); }
            else if (sz != 1) { ins0("swap"); ins1("const", sz); ins0("mul"); ins0("swap"); }
            res = rt;
        }
        un = 1;
    } else if (op == '-' && lp) {
        if (rp) {           /* ptr - ptr: a number of elements */
            u16 sz = pointee(lt);
            ins0("sub");
            if (sz != 1) { ins1("const", sz); ins0("div"); }
            e_c = 0; e_lv = 0; e_t = T_INTT;
            return;
        }
        scale(pointee(lt));
        rval = e_v;
        res = lt;
        un = 1;
    } else {
        un = is_unsigned(lt) || is_unsigned(rt);
        if (op == T_SHL || op == T_SHR) un = is_unsigned(lt);
        if (!is_cmp(op) && !(lp || rp)) res = un ? T_UINTT : T_INTT;
        if (lp || rp) { if (!is_cmp(op)) { err("bad operands for a pointer"); return; } }
    }
    if (is_cmp(op)) res = T_INTT;
    if (lc && rc) {
        v = fold(op, lval, rval, un, &ok);
        if (ok) { e_c = 1; e_v = v; e_lv = 0; e_t = res; return; }
    }
    if (rc) { ins1("const", rval); ins0(opname(op, un)); }
    else if (lc) {
        ins1("const", lval);
        if (!commutes(op)) ins0("swap");
        ins0(opname(op, un));
    } else ins0(opname(op, un));
    e_c = 0; e_lv = 0; e_t = res;
}

/* ---------------------------------------------------------------- declarations: specifiers and declarators */
static u8 in_func;

static u8 is_typename_tok(void)
{
    sym_t *s;
    if (tok >= K_VOID && tok <= K_ENUM) return 1;
    if (tok == K_CONST || tok == K_VOLATILE) return 1;
    if (tok == T_ID) { s = find_sym(toks); return s && s->cls == C_TYPEDEF; }
    return 0;
}

static int const_expr(void);
static u16 parse_declspec(u8 *storage);
static u16 declarator(u16 base, char *namep, u8 *isfn);

static u16 find_tag(const char *n)
{
    u8 i;
    for (i = 0; i < ntag; i++) if (!strcmp(tags[i].name, n)) return (u16)(i + 1);
    return 0;
}

static u16 struct_spec(u8 isunion)
{
    char name[NAMEMAX + 1];
    u16 t = 0, ti;
    name[0] = 0;
    if (tok == T_ID) { strcpy(name, toks); next(); }
    if (name[0]) {
        ti = find_tag(name);
        if (ti) t = tags[ti - 1].ty;
    }
    if (!t) {
        t = newty(isunion ? TK_UNION : TK_STRUCT, 0, 0);
        if (name[0]) {
            if (ntag >= NTAG) err("too many struct tags");
            else { strcpy(tags[ntag].name, name); tags[ntag].ty = t; ntag++; }
        }
    }
    if (tok == '{') {
        u16 size = 0, first = 0, last = 0;
        if (tys[t].n) err("a structure defined twice");
        next();
        while (tok != '}' && tok != T_EOF) {
            u8 st;
            u16 bt = parse_declspec(&st);
            for (;;) {
                char mn[NAMEMAX + 1];
                u16 mt;
                u8 isfn;
                mt = declarator(bt, mn, &isfn);
                if (nmem >= NMEM) { err("too many members"); break; }
                strcpy(mems[nmem].name, mn);
                mems[nmem].ty = mt;
                mems[nmem].next = 0;
                if (isunion) { mems[nmem].off = 0; if (tys[mt].size > size) size = tys[mt].size; }
                else { mems[nmem].off = size; size = (u16)(size + tys[mt].size); }
                nmem++;
                if (!first) first = nmem;
                if (last) mems[last - 1].next = nmem;
                last = nmem;
                if (tok == ',') { next(); continue; }
                break;
            }
            expect(';', "missing ; after a member");
        }
        expect('}', "missing }");
        tys[t].size = size;
        tys[t].n = first ? first : 0xFFFF;      /* an empty structure */
    }
    return t;
}

static u16 enum_spec(void)
{
    int v = 0;
    if (tok == T_ID) {
        next();
    }
    if (tok == '{') {
        next();
        while (tok == T_ID) {
            char n[NAMEMAX + 1];
            sym_t *s;
            strcpy(n, toks);
            next();
            if (tok == '=') { next(); v = const_expr(); }
            if (in_func) s = new_local(n, C_ENUM, T_INTT, v);
            else { s = new_global(n, C_ENUM, T_INTT); s->val = v; s->flags = 1; }
            v++;
            if (tok == ',') next(); else break;
        }
        expect('}', "missing } of the enum");
    }
    return T_INTT;
}

/* the specifiers of a declaration; storage: 1 typedef 2 static 4 extern */
static u16 parse_declspec(u8 *storage)
{
    u8 un = 0, sg = 0, ch_ = 0, in_ = 0, vd = 0, sh = 0, lg = 0, fl = 0;
    u16 t = 0;
    *storage = 0;
    for (;;) {
        if (tok == K_TYPEDEF) *storage = 1;
        else if (tok == K_STATIC) *storage = 2;
        else if (tok == K_EXTERN) *storage = 4;
        else if (tok == K_CONST || tok == K_VOLATILE || tok == K_REGISTER || tok == K_AUTO) { }
        else if (tok == K_UNSIGNED) un = 1;
        else if (tok == K_SIGNED) sg = 1;
        else if (tok == K_CHAR) ch_ = 1;
        else if (tok == K_INT) in_ = 1;
        else if (tok == K_SHORT) sh = 1;
        else if (tok == K_VOID) vd = 1;
        else if (tok == K_LONG) lg = 1;
        else if (tok == K_FLOAT || tok == K_DOUBLE) fl = 1;
        else if (tok == K_STRUCT || tok == K_UNION) { u8 u = tok == K_UNION; next(); t = struct_spec(u); continue; }
        else if (tok == K_ENUM) { next(); t = enum_spec(); continue; }
        else if (tok == T_ID && !t && !un && !sg && !ch_ && !in_ && !vd && !sh && !lg && !fl) {
            sym_t *s = find_sym(toks);
            if (s && s->cls == C_TYPEDEF) t = s->ty; else break;
        } else break;
        next();
    }
    (void)sg;
    if (t) return t;
    if (vd) return T_VOIDT;
    if (fl) return T_FLOATT;
    if (lg) return un ? T_ULONGT : T_LONGT;
    if (ch_) return un ? T_UCHART : T_CHART;
    if (in_ || sh || un || sg) return un ? T_UINTT : T_INTT;
    return T_INTT;      /* a declaration without a type is an int (K&R) */
}

/* a function declarator's parameter list (after the '('): the parameters are put into the arrays below */
static char pnames[8][NAMEMAX + 1];
static u16 ptypes[8];
static u8 npar;
static u8 pvariadic;

static void param_list(void)
{
    char ln_[8][NAMEMAX + 1];
    u16 lt_[8];
    u8 n = 0, i, var = 0;
    npar = 0;
    pvariadic = 0;
    if (tok == ')') return;
    if (tok == K_VOID && peek() == ')') { next(); return; }
    for (;;) {
        u8 st;
        u16 bt, pt;
        char pn[NAMEMAX + 1];
        u8 f;
        if (tok == T_ELLIP) { next(); var = 1; break; }
        if (!is_typename_tok()) { err("a parameter needs a type"); break; }
        bt = parse_declspec(&st);
        pt = declarator(bt, pn, &f);
        /* an array parameter is a pointer, a function a pointer to a function; a char is an int */
        if (tys[pt].k == TK_ARR) pt = ptr_to(tys[pt].base);
        else if (tys[pt].k == TK_FUNC) pt = ptr_to(pt);
        else if (tys[pt].k == TK_CHAR) pt = T_INTT;
        if (n < 8) { strcpy(ln_[n], pn); lt_[n] = pt; n++; }
        if (tok == ',') next(); else break;
    }
    for (i = 0; i < n; i++) { strcpy(pnames[i], ln_[i]); ptypes[i] = lt_[i]; }
    npar = n;
    pvariadic = var;
}

static u16 array_suffix(u16 base)
{
    if (tok == '[') {
        u16 n = 0;
        u8 open = 0;
        next();
        if (tok == ']') open = 1;
        else n = (u16)const_expr();
        expect(']', "missing ]");
        {
            u16 elem = array_suffix(base);      /* the later [] are the inner dimensions */
            u16 r = arr_of(elem, n);
            if (open) tys[r].n = 0;
            return r;
        }
    }
    return base;
}

/* a declarator: "*" ... name [n] (params) ; the name goes to namep ("" for an abstract one); isfn is set for a function declarator
 * (the parameters are in pnames, ptypes, npar) */
static u16 declarator(u16 base, char *namep, u8 *isfn)
{
    u16 t = base;
    u8 stars = 0;
    u8 paren = 0;
    *isfn = 0;
    namep[0] = 0;
    while (tok == '*') { stars++; next(); while (tok == K_CONST || tok == K_VOLATILE) next(); }
    while (stars) { t = ptr_to(t); stars--; }
    if (tok == '(' && peek() == '*') {
        /* a pointer to a function (or an array of them): the inner declarator comes first in the text but applies last */
        u8 k = 0;
        next();
        while (tok == '*') { k++; next(); }
        if (tok == T_ID) { strcpy(namep, toks); next(); }
        expect(')', "missing )");
        paren = 1;
        {
            u16 inner;
            if (tok == '(') {
                next();
                param_list();
                expect(')', "missing )");
                inner = func_of(t);
            } else inner = array_suffix(t);
            while (k) { inner = ptr_to(inner); k--; }
            return inner;
        }
    }
    (void)paren;
    if (tok == T_ID) { strcpy(namep, toks); next(); }
    if (tok == '(') {
        next();
        param_list();
        expect(')', "missing )");
        *isfn = 1;
        return func_of(t);
    }
    return array_suffix(t);
}

/* a type name (casts, sizeof): specifiers and an abstract declarator */
static u16 type_name(void)
{
    u8 st, f;
    char n[NAMEMAX + 1];
    u16 bt = parse_declspec(&st);
    return declarator(bt, n, &f);
}

/* ---------------------------------------------------------------- expressions */
static void parse_cond(void);
static void assign_tail(void);

static int const_expr(void)
{
    u16 save = alen;
    u8 sv = to_arena;
    int v;
    to_arena = 1;
    parse_cond();
    rvalue();
    v = e_v;
    if (!e_c) err("a constant expression is needed");
    to_arena = sv;
    alen = save;
    e_c = 0;
    return v;
}

/* the characters of the string token as the text of a .s line */
static void put_text(wfile *f)
{
    u16 i;
    for (i = 0; i < toklen; i++) {
        u8 c = (u8)toks[i];
        if (c >= 32 && c < 127 && c != '"' && c != '\\') w_putc(f, c);
        else {
            const char *hx = "0123456789ABCDEF";
            w_puts(f, "\\x");
            w_putc(f, hx[c >> 4]);
            w_putc(f, hx[c & 15]);
        }
    }
}

/* the string token as a data item S<n>; its address is pushed (the token is not consumed) */
static void string_data(void)
{
    char nm[8];
    u16 n = ++nstat;
    w_puts(&sf, ".data S");
    w_num(&sf, n);
    w_puts(&sf, " local\n.s \"");
    put_text(&sf);
    w_puts(&sf, "\"\n.b 0\n.end\n");
    sname(nm, 'S', n);
    insaddr(nm, 0);
}

static void string_literal(void)
{
    string_data();
    e_t = ptr_to(T_CHART);
    e_lv = 0; e_c = 0; e_fn = 0;
    next();
}

static void primary(void)
{
    e_c = 0; e_lv = 0; e_fn = 0;
    if (tok == T_NUM && (toklong || tokvh)) {
        u16 n = ++nstat;
        char nm[8];
        w_puts(&sf, ".data S"); w_num(&sf, n); w_puts(&sf, " local\n.w "); w_num(&sf, (int)tokvh); w_putc(&sf, ','); w_num(&sf, tokv); w_puts(&sf, "\n.end\n");
        sname(nm, 'S', n);
        insaddr(nm, 0);
        e_t = toklong == 2 ? T_FLOATT : tokun ? T_ULONGT : T_LONGT; e_lv = 1; if (toklong == 2) float_seen = 1;
        next();
    } else if (tok == T_NUM) {
        e_c = 1; e_v = tokv;
        e_t = (tokun || (tokv >= 32768 || tokv < -32768)) ? T_UINTT : T_INTT;
        next();
    } else if (tok == T_STR) {
        string_literal();
    } else if (tok == '(') {
        next();
        parse_expr();
        expect(')', "missing )");
    } else if (tok == T_ID) {
        sym_t *s = find_sym(toks);
        char nm[NAMEMAX + 1];
        strcpy(nm, toks);
        next();
        if (!s) {
            if (tok == '(') {           /* an undeclared function: it returns an int */
                s = new_global(nm, C_GLOBAL, func_of(T_INTT));
                s->flags = 4 | 2;
            } else { err("undeclared name"); s = new_global(nm, C_GLOBAL, T_INTT); s->flags = 2; }
        }
        e_t = s->ty;
        if (is_flt(e_t) || (tys[e_t].k == TK_FUNC && is_flt(tys[e_t].base))) float_seen = 1;
        switch (s->cls) {
        case C_ENUM: e_c = 1; e_v = s->val; e_t = T_INTT; break;
        case C_LOCAL: ins1("laddr", s->val); e_lv = 1; break;
        case C_PARAM: ins1("paddr", s->val); e_lv = 1; break;
        case C_STATIC: {
            char t[8];
            sname(t, 'V', (u16)s->val);
            insaddr(t, 0);
            e_lv = 1;
            break;
        }
        case C_GLOBAL:
            s->flags |= 2;
            if (tys[s->ty].k == TK_FUNC) e_fn = (int)(s - gsyms) + 1;
            else { insaddr(s->name, 0); e_lv = 1; }
            break;
        default: err("a type name in an expression"); break;
        }
    } else {
        err("an expression is expected");
        if (tok != T_EOF && tok != ';' && tok != '}') next();
        e_c = 1; e_v = 0; e_t = T_INTT;
    }
}

static void call(void)
{
    /* the callee is in e_*: a function name (e_fn) or a pointer value on the stack */
    u16 rt;
    int fn = e_fn;
    u16 ft = e_t;
    u8 n = 0, extra = 0;          /* extra: the second word of each long argument */
    u16 seg_s[10], seg_e[10];
    u16 base, i;
    u8 outer = to_arena;
    if (fn) { e_fn = 0; }
    else {
        if (tys[e_t].k == TK_FUNC && e_lv) { e_lv = 0; }
        else {
            rvalue();
            force();
            if (tys[e_t].k == TK_PTR && tys[tys[e_t].base].k == TK_FUNC) ft = tys[e_t].base;
            else err("the called object is not a function");
        }
    }
    rt = tys[ft].k == TK_FUNC ? tys[ft].base : T_INTT;
    next();         /* the ( */
    base = alen;
    to_arena = 1;
    if (tok != ')') {
        for (;;) {
            if (n >= 10) { err("too many arguments"); break; }
            seg_s[n] = alen;
            parse_assign();
            if (is_agg(e_t)) err("a structure as an argument");
            rv();
            if (is_wide(e_t)) { ins0("dup"); ins1("const", 2); ins0("add"); ins0("load.w"); ins0("swap"); ins0("load.w"); extra++; }     /* a long: its 2 words */
            seg_e[n] = alen;
            n++;
            if (tok == ',') next(); else break;
        }
    }
    expect(')', "missing ) of the call");
    /* the arguments from right to left */
    {
        u16 total = (u16)(alen - base);
        if (alen + total >= ARENA) { err("expression too complex"); total = 0; }
        else {
            u16 w = alen;
            for (i = n; i > 0; i--) {
                u16 len = (u16)(seg_e[i - 1] - seg_s[i - 1]);
                memcpy(arena + w, arena + seg_s[i - 1], len);
                w = (u16)(w + len);
            }
            memmove(arena + base, arena + alen, total);
            alen = (u16)(base + total);
        }
    }
    to_arena = outer;
    if (!outer) {
        /* the text goes to the output */
        u16 k;
        for (k = base; k < alen; k++) w_putc(&ir, arena[k]);
        alen = base;
    }
    if (fn) {
        put("\tcall ");
        put(gsyms[fn - 1].name);
        put(" ");
        putn(n + extra);
        put("\n");
    } else {
        ins1("calli", n + extra);
    }
    e_t = rt; e_lv = 0; e_c = 0; e_fn = 0;
    if (is_wide(rt)) {            /* the function leaves a long or float in __lret: it is copied into a temporary */
        u16 t = new_temp();
        ins0("drop");
        ins1("laddr", t);
        lh_used[LH_RET] = 1;
        insaddr("__lret", 0);
        ins1("copy", 4);
        e_lv = 1;
    }
}

static u8 stmt_top;         /* set at the start of an expression statement: the leftmost operand may be x++ with an unused value */

static void incdec(u16 op, u8 post, u8 unused)
{
    /* e_* is an lvalue (its address is on the stack) */
    u16 t = e_t;
    u16 sz = 1;
    const char *opn = op == T_INC ? "add" : "sub";
    u8 byte = tys[t].k == TK_CHAR;
    if (!e_lv || tys[t].k == TK_ARR || is_agg(t)) { err("++ or -- needs a variable"); return; }
    if (is_wide(t)) {
        int d = op == T_INC ? 1 : -1;
        u8 inc = is_flt(t) ? LH_FINC : LH_INC;
        if (post && !unused) {
            u16 tmp = new_temp();
            ins0("dup"); ins1("laddr", tmp); ins0("swap"); ins1("copy", 4); ins0("drop");
            ins1("const", d); ins0("swap"); lcall(inc, 2); ins0("drop");
            ins1("laddr", tmp);
        } else {
            ins0("dup"); ins1("const", d); ins0("swap"); lcall(inc, 2); ins0("drop");
        }
        e_lv = 1; e_c = 0;
        return;
    }
    if (is_ptr(t)) sz = pointee(t);
    if (post && !unused && !byte) {
        ins1("postinc", op == T_INC ? (int)sz : -(int)sz);     /* ( a -- old ): the word at a is changed, its old value stays */
        e_t = t;
    } else if (post && !unused) {
        ins0("dup"); ins0(byte ? (tys[t].un ? "load.ub" : "load.b") : "load.w");
        ins0("swap"); ins0("dup"); ins0(byte ? (tys[t].un ? "load.ub" : "load.b") : "load.w");
        ins1("const", sz); ins0(opn);
        ins0(byte ? "store.b" : "store.w");
        ins0("drop");
        e_t = byte ? T_INTT : t;
    } else {
        ins0("dup"); ins0(byte ? (tys[t].un ? "load.ub" : "load.b") : "load.w");
        ins1("const", sz); ins0(opn);
        ins0(byte ? "store.b" : "store.w");
        e_t = byte ? T_INTT : t;
    }
    e_lv = 0; e_c = 0;
}

static mem_t *find_member(u16 st, const char *n)
{
    u16 m = tys[st].n;
    if (m == 0xFFFF) m = 0;
    while (m) {
        if (!strcmp(mems[m - 1].name, n)) return &mems[m - 1];
        m = mems[m - 1].next;
    }
    return NULL;
}

static void postfix(void)
{
    u8 top = stmt_top;
    stmt_top = 0;
    primary();
    for (;;) {
        if (tok == '[') {
            u16 lt;
            next();
            rvalue();
            if (!is_ptr(e_t)) { err("only a pointer or an array can be indexed"); }
            force();
            lt = e_t;
            parse_expr();
            rvalue();
            expect(']', "missing ]");
            scale(pointee(lt));
            force();
            ins0("add");
            e_t = tys[lt].base;
            e_lv = 1; e_c = 0;
        } else if (tok == '(') {
            call();
        } else if (tok == '.' || tok == T_ARROW) {
            u8 arrow = tok == T_ARROW;
            mem_t *m;
            u16 st;
            next();
            if (arrow) {
                rv();
                if (tys[e_t].k != TK_PTR || !is_agg(tys[e_t].base)) { err("-> needs a pointer to a structure"); st = 0; }
                else st = tys[e_t].base;
            } else {
                if (!e_lv || !is_agg(e_t)) { err(". needs a structure"); st = 0; }
                else st = e_t;
            }
            if (tok != T_ID) { err("a member name is expected"); return; }
            m = st ? find_member(st, toks) : NULL;
            if (st && !m) err("no such member");
            next();
            if (m) {
                if (m->off) { ins1("const", m->off); ins0("add"); }
                e_t = m->ty;
            } else e_t = T_INTT;
            e_lv = 1; e_c = 0;
        } else if (tok == T_INC || tok == T_DEC) {
            u16 op = tok;
            next();
            /* the value of x++ is unused when it is a whole statement (or the increment of a for) */
            incdec(op, 1, top && (tok == ';' || tok == ')'));
            top = 0;
        } else break;
    }
}

static void parse_unary(void)
{
    u16 op = tok;
    switch (op) {
    case '-': case '+': case '~': case '!': {
        next();
        parse_cast();
        rvalue();
        if (op == '+') return;
        if (is_wide(e_t)) {
            u16 t;
            if (op == '!') { lcall(is_flt(e_t) ? LH_FNZ : LH_NZ, 1); e_t = T_INTT; e_lv = 0; ins0("not"); return; }
            if (op == '~' && is_flt(e_t)) { err("~ of a float"); return; }
            t = new_temp();
            ins1("laddr", t);
            lcall(op == '-' ? (is_flt(e_t) ? LH_FNEG : LH_NEG) : LH_COM, 2);
            ins0("drop");
            ins1("laddr", t);
            e_lv = 1;
            return;
        }
        if (e_c) {
            if (op == '-') e_v = -e_v;
            else if (op == '~') e_v = ~e_v;
            else e_v = !e_v;
            if (op == '!') e_t = T_INTT;
            return;
        }
        ins0(op == '-' ? "neg" : op == '~' ? "com" : "not");
        if (op == '!') e_t = T_INTT;
        return;
    }
    case '*': {
        u16 bt;
        next();
        parse_cast();
        rv();
        if (tys[e_t].k != TK_PTR) { err("* needs a pointer"); e_t = ptr_to(T_INTT); }
        bt = tys[e_t].base;
        if (tys[bt].k == TK_VOID) err("* on a void pointer");
        e_t = bt;
        e_lv = 1;
        return;
    }
    case '&': {
        next();
        parse_cast();
        if (e_fn) { rvalue(); return; }
        if (!e_lv) { err("& needs an lvalue"); return; }
        e_t = ptr_to(e_t);
        e_lv = 0;
        return;
    }
    case T_INC: case T_DEC: {
        next();
        parse_cast();
        incdec(op, 0, 0);
        return;
    }
    case K_SIZEOF: {
        u16 t;
        u16 save = alen;
        u8 sv = to_arena;
        next();
        to_arena = 1;
        if (tok == '(') {
            next();
            if (is_typename_tok()) { t = type_name(); expect(')', "missing )"); }
            else {
                parse_expr();
                expect(')', "missing )");
                t = e_t;
            }
        } else {
            parse_unary();
            t = e_t;
        }
        to_arena = sv;
        alen = save;
        e_c = 1; e_v = (int)tys[t].size; e_lv = 0; e_fn = 0; e_t = T_UINTT;
        return;
    }
    default:
        postfix();
    }
}

static void parse_cast(void)
{
    if (tok == '(' ) {
        u16 pk = peek();
        sym_t *s;
        u8 istype = 0;
        if (pk >= K_VOID && pk <= K_ENUM) istype = 1;
        else if (pk == K_CONST || pk == K_VOLATILE) istype = 1;
        if (!istype && pk == T_ID) {
            /* the identifier after ( is not known until it is the current token: look it up in the peeked text */
            s = find_sym(ptoks);
            istype = s && s->cls == C_TYPEDEF;
        }
        if (istype) {
            u16 t;
            next();
            t = type_name();
            expect(')', "missing ) of the cast");
            parse_cast();
            rvalue();
            if (is_wide(t)) {
                to_wide(t);
                e_t = t;
                return;
            }
            if (is_wide(e_t)) wide_to_int();
            if (tys[t].k == TK_CHAR) {
                if (e_c) e_v = tys[t].un ? (e_v & 255) : (int)(s8)e_v;
                else ins0(tys[t].un ? "zx" : "sx");
                e_t = t;
                e_t = tys[t].un ? T_UINTT : T_INTT;
            } else {
                if (is_agg(t) || is_agg(e_t)) err("a structure in a cast");
                e_t = t;
            }
            return;
        }
    }
    parse_unary();
}

/* binary operators by level */
static u8 level_of(u16 t)
{
    switch (t) {
    case '|': return 0;
    case '^': return 1;
    case '&': return 2;
    case T_EQ: case T_NE: return 3;
    case '<': case '>': case T_LE: case T_GE: return 4;
    case T_SHL: case T_SHR: return 5;
    case '+': case '-': return 6;
    case '*': case '/': case '%': return 7;
    }
    return 255;
}

static void parse_bin(u8 lv)
{
    if (lv == 8) { parse_cast(); return; }
    parse_bin((u8)(lv + 1));
    while (level_of(tok) == lv) {
        u16 op = tok;
        u8 lc;
        int lval;
        u16 lt;
        rvalue();
        lc = e_c; lval = e_v; lt = e_t;
        next();
        parse_bin((u8)(lv + 1));
        rvalue();
        binary(op, lc, lval, lt);
    }
}

/* && and || as a value */
static void parse_logand(void)
{
    parse_bin(0);
    if (tok == T_ANDAND) {
        u16 lf = ++nlab, le = ++nlab;
        truth();
        insl("jz", lf);
        while (tok == T_ANDAND) {
            next();
            parse_bin(0);
            truth();
            insl("jz", lf);
        }
        ins1("const", 1);
        insl("jmp", le);
        deflabel(lf);
        ins1("const", 0);
        deflabel(le);
        e_t = T_INTT; e_c = 0; e_lv = 0;
    }
}

static void parse_logor(void)
{
    parse_logand();
    if (tok == T_OROR) {
        u16 lt = ++nlab, le = ++nlab;
        truth();
        insl("jnz", lt);
        while (tok == T_OROR) {
            next();
            parse_logand();
            truth();
            insl("jnz", lt);
        }
        ins1("const", 0);
        insl("jmp", le);
        deflabel(lt);
        ins1("const", 1);
        deflabel(le);
        e_t = T_INTT; e_c = 0; e_lv = 0;
    }
}

static void parse_cond(void)
{
    parse_logor();
    if (tok == '?') {
        u16 lf = ++nlab, le = ++nlab;
        u16 t1, t2;
        truth();
        insl("jz", lf);
        next();
        parse_expr();
        rv();
        t1 = e_t;
        insl("jmp", le);
        expect(':', "missing : of ?:");
        deflabel(lf);
        parse_cond();
        rv();
        t2 = e_t;
        deflabel(le);
        e_t = is_ptr(t1) ? t1 : is_ptr(t2) ? t2 : (is_unsigned(t1) || is_unsigned(t2)) ? T_UINTT : T_INTT;
        e_c = 0; e_lv = 0;
        if (is_wide(t1) && tys[t1].k == tys[t2].k) { e_t = t1; e_lv = 1; }
        else if (is_wide(t1) || is_wide(t2)) err("a long or float and another type in ?: (use a cast)");
    }
}

/* a condition: falls through when it is true, jumps to lf when it is false; nothing is left on the stack */
static void cond_and(u16 lf)
{
    for (;;) {
        parse_bin(0);
        truth();
        insl("jz", lf);
        if (tok == T_ANDAND) { next(); continue; }
        break;
    }
}

/* a condition from a full expression: `if (a = b)` and the like are values, && || at the top are jumps */
static void condition(u16 lf)
{
    /* the common forms (comparisons, &&, ||, calls) go through cond_or; an assignment or ?: at the top level is parsed as a value */
    parse_bin(0);
    if (tok == T_ANDAND || tok == T_OROR) {
        /* the first operand is parsed already: finish with the loops of cond_and and cond_or by hand */
        u16 lt = 0;
        u16 ln = ++nlab;
        truth();
        insl("jz", ln);
        while (tok == T_ANDAND) {
            next();
            parse_bin(0);
            truth();
            insl("jz", ln);
        }
        while (tok == T_OROR) {
            if (!lt) lt = ++nlab;
            insl("jmp", lt);
            deflabel(ln);
            next();
            ln = ++nlab;
            cond_and(ln);
        }
        aliaslabel(ln, lf);
        if (lt) deflabel(lt);
        return;
    }
    if (tok == '=' || (tok >= T_ADDEQ && tok <= T_SHREQ)) assign_tail();
    else if (tok == '?') err("?: at the top of a condition needs parentheses");
    truth();
    insl("jz", lf);
}

static void assign_op(u16 op)
{
    /* e_* is an lvalue (address on the stack); op: '=' or the operator of a compound assignment ('+' ...) */
    u16 lt = e_t;
    u8 byte = tys[lt].k == TK_CHAR;
    if (!e_lv) { err("the left side of an assignment is not a variable"); return; }
    if (tys[lt].k == TK_ARR || tys[lt].k == TK_FUNC) { err("an array or function cannot be assigned"); return; }
    if (op == '=') {
        parse_assign();
        rvalue();
        if (is_wide(lt)) {
            to_wide(lt);
            ins1("copy", 4);
            e_t = lt; e_lv = 1; e_c = 0;
            return;
        }
        if (is_wide(e_t)) wide_to_int();
        if (is_agg(lt)) {
            if (!is_agg(e_t) || tys[e_t].size != tys[lt].size) { err("incompatible structures"); return; }
            ins1("copy", (int)tys[lt].size);
            e_t = lt; e_lv = 0;
            return;
        }
        force();
        ins0(byte ? "store.b" : "store.w");
    } else {
        u8 lc;
        u16 lt2;
        int lval;
        if (is_agg(lt)) { err("a structure in a compound assignment"); return; }
        if (is_wide(lt)) {                 /* L op= x : a = L, b = x, d = L */
            u8 shift = op == T_SHL || op == T_SHR;
            if (shift && is_flt(lt)) { err("a shift of a float"); return; }
            ins0("dup");
            parse_assign();
            rvalue();
            if (shift) { if (is_wide(e_t)) wide_to_int(); force(); } else to_wide(lt);
            ins0("swap");
            lcall(wide_op_id(op, lt), 3);
            ins0("drop");
            e_t = lt; e_lv = 1; e_c = 0;
            return;
        }
        ins0("dup");
        ins0(byte ? (tys[lt].un ? "load.ub" : "load.b") : "load.w");
        lt2 = byte ? T_INTT : lt;
        lc = 0; lval = 0;
        parse_assign();
        rvalue();
        binary(op, lc, lval, lt2);
        if (is_wide(e_t)) wide_to_int();
        force();
        ins0(byte ? "store.b" : "store.w");
    }
    e_t = byte ? T_INTT : lt;
    e_lv = 0; e_c = 0;
}

static void assign_tail(void)
{
    if (tok == '=') { next(); assign_op('='); }
    else if (tok >= T_ADDEQ && tok <= T_SHREQ) {
        static const u16 ops[] = { '+', '-', '*', '/', '%', '&', '|', '^', T_SHL, T_SHR };
        u16 op = ops[tok - T_ADDEQ];
        next();
        assign_op(op);
    }
}

static void parse_assign(void)
{
    parse_cond();
    assign_tail();
}

static void parse_expr(void)
{
    parse_assign();
    while (tok == ',') {
        rv();
        ins0("drop");
        next();
        parse_assign();
    }
}

/* ---------------------------------------------------------------- declarations of variables and functions */
static u16 frame_patch, frame_ph;        /* where in the output the size of the frame of this function is written */
static u16 cur_ret;
static const char *curfn;

static void data_item_label(const char *name, u8 local)
{
    w_puts(&df, ".data ");
    w_puts(&df, name);
    if (local) w_puts(&df, " local");
    w_putc(&df, '\n');
}

/* the initializer of a global or static variable: .b .w .wa lines into f; t is the type; returns the bytes written */
static u16 init_data(wfile *f, u16 t);

static void emit_zero(wfile *f, u16 n)
{
    if (n) { w_puts(f, ".z "); w_num(f, n); w_putc(f, '\n'); }
}

/* the bits of the float that is the int v (unsigned if un) */
static void int_bits(int v, u8 un, u16 *hi, u16 *lo)
{
    u16 sign = 0, a, e = 15;
    if (!un && v < 0) { sign = 0x8000; a = (u16)(-v); } else a = (u16)v;
    if (!a) { *hi = 0; *lo = 0; return; }
    while (!(a & 0x8000)) { a <<= 1; e--; }
    *hi = sign | ((127 + e) << 7) | ((a >> 8) & 0x7F);
    *lo = (a & 0xFF) << 8;
}

/* an initializer expression that must be constant or the address of something: .b/.w/.wa */
static void init_scalar(wfile *f, u16 t)
{
    u16 save = alen;
    u8 sv = to_arena;
    if (is_wide(t) && (tok == T_NUM || (tok == '-' && peek() == T_NUM))) {      /* a long or float constant */
        u16 hi, lo;
        u8 neg = tok == '-';
        if (neg) next();
        hi = tokvh; lo = (u16)tokv;
        if (is_flt(t)) {
            if (!toklong) int_bits(tokv, tokun, &hi, &lo);          /* (an int: float f = 1;) */
            if (neg) hi ^= 0x8000;
        } else if (neg) { lo = (u16)(~lo + 1); hi = (u16)(~hi + (lo == 0)); }
        w_puts(f, ".w "); w_num(f, (int)hi); w_putc(f, ','); w_num(f, (int)lo); w_putc(f, '\n');
        next();
        return;
    }
    to_arena = 1;
    parse_assign();
    rvalue();
    to_arena = sv;
    if (e_c) {
        if (is_flt(t)) {
            u16 hi, lo;
            int_bits(e_v, is_unsigned(e_t), &hi, &lo);
            w_puts(f, ".w "); w_num(f, (int)hi); w_putc(f, ','); w_num(f, (int)lo);
        } else {
            if (is_long(t)) {
                w_puts(f, ".w "); w_num(f, e_v < 0 && !is_unsigned(e_t) ? -1 : 0); w_putc(f, ',');
            } else w_puts(f, tys[t].size == 1 ? ".b " : ".w ");
            w_num(f, e_v);
        }
        w_putc(f, '\n');
    } else {
        /* addr NAME [const k add]... */
        char name[24];
        int off = 0;
        char *p = arena + save;
        char *end = arena + alen;
        u8 okf = 0;
        arena[alen] = 0;
        if (!strncmp(p, "\taddr ", 6)) {
            u8 i = 0;
            p += 6;
            while (p < end && *p != '\n' && *p != '+' && *p != '-' && i < 22) name[i++] = *p++;
            name[i] = 0;
            if (*p == '+' || *p == '-') { off = cc_atoi(p); while (*p != '\n') p++; }
            p++;
            okf = 1;
            while (p < end) {
                if (!strncmp(p, "\tconst ", 7)) {
                    int k = cc_atoi(p + 7);
                    while (p < end && *p != '\n') p++;
                    p++;
                    if (p < end && !strncmp(p, "\tadd\n", 5)) { off += k; p += 5; }
                    else { okf = 0; break; }
                } else { okf = 0; break; }
            }
        }
        if (okf) {
            w_puts(f, ".wa ");
            w_puts(f, name);
            if (off) { if (off > 0) w_putc(f, '+'); w_num(f, off); }
            w_putc(f, '\n');
        } else {
            err("the initializer is not constant");
            w_puts(f, ".w 0\n");
        }
    }
    alen = save;
    e_c = 0;
}

static u16 init_data(wfile *f, u16 t)
{
    u8 k = tys[t].k;
    if (k == TK_ARR) {
        u16 et = tys[t].base;
        u16 n = tys[t].n, i = 0;
        if (tok == T_STR && tys[et].k == TK_CHAR) {
            w_puts(f, ".s \"");
            put_text(f);
            w_puts(f, "\"\n");
            i = toklen;
            if (!n || i < n) { w_puts(f, ".b 0\n"); i++; }
            if (n && toklen > n) err("the string is too long for the array");
            if (!n) tys[t].n = i, tys[t].size = i;
            else if (i < n) emit_zero(f, (u16)(n - i));
            next();
            return tys[t].size;
        }
        if (tok == '{') {
            next();
            while (tok != '}' && tok != T_EOF) {
                init_data(f, et);
                i++;
                if (tok == ',') next(); else break;
            }
            expect('}', "missing } of the initializer");
            if (!n) { tys[t].n = i; tys[t].size = (u16)(i * tys[et].size); }
            else if (i < n) emit_zero(f, (u16)((n - i) * tys[et].size));
            else if (i > n) err("too many initializers");
            return tys[t].size;
        }
        err("an array needs { } to be initialized");
        return tys[t].size;
    }
    if (is_agg(t)) {
        u16 m = tys[t].n;
        u16 done = 0;
        if (tok != '{') { err("a structure needs { } to be initialized"); return tys[t].size; }
        next();
        if (m == 0xFFFF) m = 0;
        while (tok != '}' && tok != T_EOF && m) {
            init_data(f, mems[m - 1].ty);
            done = (u16)(done + tys[mems[m - 1].ty].size);
            m = mems[m - 1].next;
            if (k == TK_UNION) break;
            if (tok == ',') next(); else break;
        }
        if (tok == ',') next();
        expect('}', "missing } of the initializer");
        if (done < tys[t].size) emit_zero(f, (u16)(tys[t].size - done));      /* the members that are left are 0 */
        return tys[t].size;
    }
    if (tok == '{') {       /* int x = { 1 }; */
        next();
        init_scalar(f, t);
        if (tok == ',') next();
        expect('}', "missing }");
        return tys[t].size;
    }
    init_scalar(f, t);
    return tys[t].size;
}

/* a local variable with an initializer: code */
static void local_init(u16 off, u16 t)
{
    u8 k = tys[t].k;
    if (k == TK_ARR) {
        u16 et = tys[t].base, n = tys[t].n, i = 0;
        u16 es = tys[et].size;
        if (tok == T_STR && tys[et].k == TK_CHAR) {
            u16 len = toklen + 1;
            if (!n) { n = len; tys[t].n = n; tys[t].size = n; }
            /* copy the string from a literal */
            ins1("laddr", off);
            string_data();
            ins1("copy", len > n ? n : len);
            ins0("drop");
            next();
            return;
        }
        if (tok == '{') {
            next();
            while (tok != '}' && tok != T_EOF) {
                local_init((u16)(off + i * es), et);
                i++;
                if (tok == ',') next(); else break;
            }
            expect('}', "missing } of the initializer");
            if (!n) { tys[t].n = i; tys[t].size = (u16)(i * es); }
            return;
        }
        err("an array needs { } to be initialized");
        return;
    }
    if (is_agg(t)) {
        u16 m = tys[t].n;
        if (tok == '{') {
            next();
            if (m == 0xFFFF) m = 0;
            while (tok != '}' && tok != T_EOF && m) {
                local_init((u16)(off + mems[m - 1].off), mems[m - 1].ty);
                m = mems[m - 1].next;
                if (tys[t].k == TK_UNION) break;
                if (tok == ',') next(); else break;
            }
            if (tok == ',') next();
            expect('}', "missing } of the initializer");
            return;
        }
        /* struct s = other; */
        ins1("laddr", off);
        parse_assign();
        rvalue();
        if (!is_agg(e_t) || tys[e_t].size != tys[t].size) err("incompatible structures");
        ins1("copy", (int)tys[t].size);
        ins0("drop");
        return;
    }
    {
        u8 br = 0;
        if (tok == '{') { br = 1; next(); }
        ins1("laddr", off);
        parse_assign();
        if (is_wide(t)) {
            rvalue();
            to_wide(t);
            ins1("copy", 4);
            ins0("drop");
        } else {
            rv();
            if (is_wide(e_t)) wide_to_int();
            ins0(tys[t].k == TK_CHAR ? "store.b" : "store.w");
            ins0("drop");
        }
        if (br) { if (tok == ',') next(); expect('}', "missing }"); }
    }
}

static void local_decl(void)
{
    u8 st;
    u16 bt = parse_declspec(&st);
    if (tok == ';') { next(); return; }     /* a struct or enum declaration */
    for (;;) {
        char nm[NAMEMAX + 1];
        u8 isfn;
        u16 t = declarator(bt, nm, &isfn);
        if (st == 1) {
            new_local(nm, C_TYPEDEF, t, 0);
        } else if (isfn || tys[t].k == TK_FUNC) {
            /* a function declared in a block */
            sym_t *g = find_global(nm);
            if (!g) { g = new_global(nm, C_GLOBAL, t); g->flags = 4; }
        } else if (st == 4) {
            sym_t *g = find_global(nm);
            if (!g) { g = new_global(nm, C_GLOBAL, t); }
        } else if (st == 2) {
            sym_t *s;
            u16 u = ++nstat;
            char vn[10];
            s = new_local(nm, C_STATIC, t, (int)u);
            (void)s;
            sname(vn, 'V', u);
            if (tok == '=') {
                next();
                data_item_label(vn, 1);
                init_data(&df, t);
                w_puts(&df, ".end\n");
            } else {
                w_puts(&df, ".bss ");
                w_puts(&df, vn);
                w_putc(&df, ' ');
                w_num(&df, tys[t].size);
                w_puts(&df, " local\n");
            }
        } else {
            u16 off;
            if (tys[t].k == TK_VOID) err("a variable of type void");
            if (tys[t].k == TK_ARR && !tys[t].n && tok != '=') err("an array needs a size");
            off = fp;
            if (tok == '=') {
                /* the size of an array without a size comes from the initializer: declare first */
                new_local(nm, C_LOCAL, t, (int)off);
                next();
                local_init(off, t);
                fp = (u16)(fp + tys[t].size);
            } else {
                new_local(nm, C_LOCAL, t, (int)off);
                fp = (u16)(fp + tys[t].size);
            }
            if (fp > maxfp) maxfp = fp;
        }
        if (tok == ',') { next(); continue; }
        break;
    }
    expect(';', "missing ; after a declaration");
}

/* ---------------------------------------------------------------- statements */
static u16 brk_l[12], cont_l[12];
static u8 nloop;
static struct { int v; u16 l; } cases[NCASE];
static u8 ncase;
static u16 dflt_l;
static u8 sw_depth;
static char lab_name[NLAB][NAMEMAX + 1];
static u16 lab_id[NLAB];
static u8 lab_def[NLAB];
static u8 nlabn;

static u16 user_label(const char *n)
{
    u8 i;
    for (i = 0; i < nlabn; i++) if (!strcmp(lab_name[i], n)) return i;
    if (nlabn >= NLAB) { err("too many labels"); return 0; }
    strcpy(lab_name[nlabn], n);
    lab_id[nlabn] = ++nlab;
    lab_def[nlabn] = 0;
    return nlabn++;
}

static void expr_statement(void)
{
    tmp_used = 0;
    stmt_top = 1;
    parse_expr();
    stmt_top = 0;
    if (e_fn) rvalue();
    if (!e_c) ins0("drop");
    e_c = 0; e_lv = 0;
}

static void block(void)
{
    u16 sv_nl = nlsym, sv_fp = fp;
    expect('{', "missing {");
    while (tok != '}' && tok != T_EOF) {
        if (is_typename_tok() || tok == K_STATIC || tok == K_EXTERN || tok == K_TYPEDEF || tok == K_REGISTER || tok == K_AUTO) {
            if (tok == T_ID && peek() == ':') statement();
            else local_decl();
        } else statement();
    }
    expect('}', "missing }");
    nlsym = sv_nl;
    fp = sv_fp;
}

static void statement(void)
{
    tmp_used = 0;
    switch (tok) {
    case ';': next(); return;
    case '{': block(); return;
    case K_IF: {
        u16 lf = ++nlab, le;
        next();
        expect('(', "missing ( after if");
        condition(lf);
        expect(')', "missing )");
        statement();
        if (tok == K_ELSE) {
            le = ++nlab;
            insl("jmp", le);
            deflabel(lf);
            next();
            statement();
            deflabel(le);
        } else deflabel(lf);
        return;
    }
    case K_WHILE: {
        u16 lt = ++nlab, le = ++nlab;
        next();
        deflabel(lt);
        expect('(', "missing ( after while");
        condition(le);
        expect(')', "missing )");
        brk_l[nloop] = le; cont_l[nloop] = lt; nloop++;
        statement();
        nloop--;
        insl("jmp", lt);
        deflabel(le);
        return;
    }
    case K_DO: {
        u16 lt = ++nlab, lc = ++nlab, le = ++nlab;
        next();
        deflabel(lt);
        brk_l[nloop] = le; cont_l[nloop] = lc; nloop++;
        statement();
        nloop--;
        deflabel(lc);
        if (tok != K_WHILE) { err("while expected"); return; }
        next();
        expect('(', "missing (");
        parse_expr();
        truth();
        insl("jnz", lt);
        expect(')', "missing )");
        expect(';', "missing ;");
        deflabel(le);
        return;
    }
    case K_FOR: {
        u16 lt = ++nlab, lc = ++nlab, le = ++nlab, lb = ++nlab;
        u8 has_inc = 0;
        u16 sv_nl = nlsym, sv_fp = fp;
        next();
        expect('(', "missing ( after for");
        if (tok != ';') {
            if (is_typename_tok()) { local_decl(); }
            else { expr_statement(); expect(';', "missing ;"); }
        } else next();
        deflabel(lt);
        if (tok != ';') condition(le);
        expect(';', "missing ;");
        /* the increment is compiled before the body: jump over it */
        insl("jmp", lb);
        deflabel(lc);
        if (tok != ')') { has_inc = 1; expr_statement(); }
        insl("jmp", lt);
        expect(')', "missing )");
        deflabel(lb);
        (void)has_inc;
        brk_l[nloop] = le; cont_l[nloop] = lc; nloop++;
        statement();
        nloop--;
        insl("jmp", lc);
        deflabel(le);
        nlsym = sv_nl;
        fp = sv_fp;
        return;
    }
    case K_SWITCH: {
        u16 ldisp = ++nlab, le = ++nlab, tmp;
        u16 sv_dflt = dflt_l;
        u8 sv_ncase = ncase;
        u8 i;
        next();
        expect('(', "missing ( after switch");
        tmp = fp;
        fp = (u16)(fp + 2);
        if (fp > maxfp) maxfp = fp;
        ins1("laddr", tmp);
        parse_expr();
        rv();
        ins0("store.w");
        ins0("drop");
        expect(')', "missing )");
        insl("jmp", ldisp);
        dflt_l = 0;
        brk_l[nloop] = le; cont_l[nloop] = nloop ? cont_l[nloop - 1] : 0; nloop++;
        sw_depth++;
        statement();
        sw_depth--;
        nloop--;
        insl("jmp", le);
        deflabel(ldisp);
        for (i = sv_ncase; i < ncase; i++) {
            ins1("laddr", tmp);
            ins0("load.w");
            ins1("const", cases[i].v);
            ins0("eq");
            insl("jnz", cases[i].l);
        }
        insl("jmp", dflt_l ? dflt_l : le);
        deflabel(le);
        ncase = sv_ncase;
        dflt_l = sv_dflt;
        fp = tmp;
        return;
    }
    case K_CASE: {
        int v;
        u16 l = ++nlab;
        next();
        v = const_expr();
        expect(':', "missing : after case");
        if (!sw_depth) err("case outside a switch");
        else if (ncase >= NCASE) err("too many cases");
        else { cases[ncase].v = v; cases[ncase].l = l; ncase++; }
        deflabel(l);
        statement();
        return;
    }
    case K_DEFAULT: {
        u16 l = ++nlab;
        next();
        expect(':', "missing : after default");
        if (!sw_depth) err("default outside a switch");
        dflt_l = l;
        deflabel(l);
        statement();
        return;
    }
    case K_BREAK:
        next();
        if (!nloop) err("break outside a loop or switch"); else insl("jmp", brk_l[nloop - 1]);
        expect(';', "missing ;");
        return;
    case K_CONTINUE: {
        next();
        {
            s8 i = (s8)nloop - 1;
            /* the innermost loop (a switch keeps the continue label of the loop outside) */
            if (i < 0 || !cont_l[i]) err("continue outside a loop"); else insl("jmp", cont_l[i]);
        }
        expect(';', "missing ;");
        return;
    }
    case K_RETURN:
        next();
        if (tok != ';') {
            parse_expr();
            rvalue();
            if (is_wide(fn_ret)) {          /* a long or float goes to __lret */
                to_wide(fn_ret);
                lh_used[LH_RET] = 1;
                insaddr("__lret", 0);
                ins0("swap");
                ins1("copy", 4);
                ins0("drop");
                ins1("const", 0);
            } else {
                if (is_wide(e_t)) wide_to_int();
                force();
            }
        } else ins1("const", 0);
        ins0("ret");
        expect(';', "missing ;");
        return;
    case K_GOTO: {
        u16 i;
        next();
        if (tok != T_ID) { err("a label is expected"); return; }
        i = user_label(toks);
        insl("jmp", lab_id[i]);
        lab_def[i] |= 2;
        next();
        expect(';', "missing ;");
        return;
    }
    default:
        break;
    }
    if (tok == T_ID && peek() == ':') {
        u16 i = user_label(toks);
        if (lab_def[i] & 1) err("a label defined twice");
        lab_def[i] |= 1;
        deflabel(lab_id[i]);
        next();
        next();
        statement();
        return;
    }
    expr_statement();
    expect(';', "missing ; after an expression");
}

/* ---------------------------------------------------------------- functions and the top level */
static void function_body(const char *name, u16 ft, u8 isstatic)
{
    u8 i;
    u8 np = npar;
    char pn[8][NAMEMAX + 1];
    u16 pt[8];
    for (i = 0; i < np; i++) { strcpy(pn[i], pnames[i]); pt[i] = ptypes[i]; }
    u16 slot = 0;
    fn_ret = tys[ft].base;
    nlsym = 0;
    fp = 0; maxfp = 0;
    nloop = 0; ncase = 0; sw_depth = 0; nlabn = 0;
    for (i = 0; i < np; i++) { new_local(pn[i], C_PARAM, pt[i], (int)slot); slot = (u16)(slot + (is_wide(pt[i]) ? 2 : 1)); }
    curfn = name;
    w_puts(&ir, ".func ");
    w_puts(&ir, name);
    w_puts(&ir, " ");
    frame_patch = w_tell(&ir);
    frame_ph = w_tellh(&ir);
    w_puts(&ir, "00000");
    if (isstatic) w_puts(&ir, " local");
    w_putc(&ir, '\n');
    in_func = 1;
    block();
    in_func = 0;
    for (i = 0; i < nlabn; i++) if ((lab_def[i] & 2) && !(lab_def[i] & 1)) err("a label is used but not defined");
    w_puts(&ir, ".end\n");
    {
        char t[6];
        u16 u = maxfp;
        u8 k;
        for (k = 5; k > 0; k--) { t[k - 1] = (char)('0' + u % 10); u /= 10; }
        w_patch(&ir, frame_ph, frame_patch, t, 5);
    }
    nlsym = 0;
}

static void external_decl(void)
{
    u8 st;
    u16 bt;
    if (tok == ';') { next(); return; }
    bt = parse_declspec(&st);
    if (tok == ';') { next(); return; }         /* struct x {...}; or enum {...}; */
    for (;;) {
        char nm[NAMEMAX + 1];
        u8 isfn;
        u16 t = declarator(bt, nm, &isfn);
        sym_t *g;
        if (!nm[0]) { err("a name is expected"); next(); return; }
        if (st == 1) {
            g = new_global(nm, C_TYPEDEF, t);
            g->flags = 1;
        } else if (tys[t].k == TK_FUNC) {
            g = find_global(nm);
            if (!g) { g = new_global(nm, C_GLOBAL, t); g->flags = 4; }
            if (st == 2) g->flags |= 8;
            if (tok == '{') {
                if (g->flags & 1) err("a function defined twice");
                g->flags |= 1;
                g->ty = t;
                function_body(nm, t, (g->flags & 8) != 0);
                return;
            }
        } else {
            u16 size;
            g = find_global(nm);
            if (!g) g = new_global(nm, C_GLOBAL, t);
            else g->ty = t;
            if (st == 2) g->flags |= 8;
            if (st != 4) {
                if (tok == '=') {
                    next();
                    data_item_label(nm, (g->flags & 8) != 0);
                    {
                        wfile *f = &df;
                        init_data(f, t);
                    }
                    w_puts(&df, ".end\n");
                    g->flags |= 1;
                } else if (!(g->flags & 1)) {
                    /* a tentative definition */
                    if (tys[t].k == TK_ARR && !tys[t].n) err("an array needs a size");
                    size = tys[t].size;
                    w_puts(&df, ".bss ");
                    w_puts(&df, nm);
                    w_putc(&df, ' ');
                    w_num(&df, size);
                    if (g->flags & 8) w_puts(&df, " local");
                    w_putc(&df, '\n');
                    g->flags |= 1;
                }
            }
        }
        if (tok == ',') { next(); continue; }
        break;
    }
    expect(';', "missing ; after a declaration");
}

int main(int argc, char **argv)
{
    int i, tail = tail_start(argc, argv);
    const char *src = NULL, *dst = NULL;
    char outname[120];
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < tail) dst = argv[++i];
        else if (!strcmp(argv[i], "-v")) return cc_version("cc1", TOOL_VERSION);
        else if (argv[i][0] == '-') { err("unknown option"); return 2; }
        else src = argv[i];
    }
    if (!src) {
#ifdef CC_HOST
        fputs("usage: cc1 [-o out.ir] file.i\n", stderr);
#else
        putstr("usage: cc1 [-o out.ir] file.i\n");
#endif
        return 2;
    }
    if (!dst) {
        u16 n = (u16)strlen(src);
        if (n > 115) n = 115;
        memcpy(outname, src, n);
        outname[n] = 0;
        if (n > 2 && outname[n - 2] == '.') { outname[n - 1] = 'i'; outname[n] = 'r'; outname[n + 1] = 0; }
        else strcat(outname, ".ir");
        dst = outname;
    }
    strcpy(fname, src);
    if (r_open(&in, src)) { err("cannot open the input"); return 2; }
    if (w_open(&ir, dst)) { err("cannot create the output"); return 2; }
    if (w_open(&df, "cc1.tmp")) { err("cannot create cc1.tmp"); return 2; }
    if (w_open(&sf, "cc1s.tmp")) { err("cannot create cc1s.tmp"); return 2; }
    tys[T_VOIDT].k = TK_VOID;
    tys[T_CHART].k = TK_CHAR; tys[T_CHART].size = 1;
    tys[T_UCHART].k = TK_CHAR; tys[T_UCHART].size = 1; tys[T_UCHART].un = 1;
    tys[T_INTT].k = TK_INT; tys[T_INTT].size = 2;
    tys[T_UINTT].k = TK_INT; tys[T_UINTT].size = 2; tys[T_UINTT].un = 1;
    tys[T_LONGT].k = TK_LONG; tys[T_LONGT].size = 4;
    tys[T_ULONGT].k = TK_LONG; tys[T_ULONGT].size = 4; tys[T_ULONGT].un = 1;
    tys[T_FLOATT].k = TK_FLOAT; tys[T_FLOATT].size = 4;
    nextch();
    line = 1;
    if (ch == '#') read_marker();
    next();
    while (tok != T_EOF && errors < 20) external_decl();
    /* undefined functions and variables that were used */
    for (i = 0; i < ngsym; i++) {
        if (gsyms[i].cls == C_GLOBAL && (gsyms[i].flags & 2) && !(gsyms[i].flags & 1)) {
            w_puts(&ir, ".extern ");
            w_puts(&ir, gsyms[i].name);
            w_putc(&ir, '\n');
        }
    }
    if (float_seen && !find_global("__fmtfp")) {
        w_puts(&ir, ".extern __fmtf\n");
        w_puts(&df, ".data __fmtfp\n.wa __fmtf\n.end\n");
    }
    for (i = 0; i < LH_N; i++)
        if (lh_used[i]) { w_puts(&ir, ".extern "); w_puts(&ir, lh_name[i]); w_putc(&ir, '\n'); }
    w_close(&df);
    w_close(&sf);
    r_close(&in);
    /* the data and string files go behind the code */
    {
        static char *names[2] = { "cc1.tmp", "cc1s.tmp" };
        u8 k;
        for (k = 0; k < 2; k++) {
            rfile r;
            int c;
            if (!r_open(&r, names[k])) {
                while ((c = r_getc(&r)) >= 0) w_putc(&ir, c);
                r_close(&r);
            }
#ifdef CC_HOST
            remove(names[k]);
#else
            unlink(names[k]);
#endif
        }
    }
    w_close(&ir);
    if (errors) {
#ifdef CC_HOST
        remove(dst);
#else
        unlink(dst);
#endif
        return 1;
    }
    return run_tail(argc, argv, tail);
}
