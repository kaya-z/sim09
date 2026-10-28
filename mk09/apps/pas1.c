/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pas1 - the front end of the Pascal compiler of MK09: Pascal in, IR out (docs/IR.md, docs/PASCAL.md)
 *
 *   pas1 [-o out.ir] file.pas
 *
 * One pass, no tree: the expressions are written to the IR as they are read (the IR is a stack machine).  The IR goes to cg, then to as, like the C compiler.
 * Integers are 16 bits.  A procedure takes its arguments from the left, the first one pushed first (so the argument j of n is `paddr n-1-j`); a procedure
 * that is nested in another gets the frame of the outer one as one more argument, pushed first, which it keeps at offset 0 of its own frame (the static link).
 */
#include "ccio.h"

#ifdef CC_HOST
#define NSYM    400
#define NTY     300
#define NFLD    300
#define NPAR    200
#else
#define NSYM    130
#define NTY     90
#define NFLD    90
#define NPAR    70
#endif
#define NAMEMAX 15
#define STRMAX  160
#define NLEV    8
#define NWITH   6

/* ---------------------------------------------------------------- tokens */
enum {
    T_EOF = 0, T_NUM = 128, T_ID, T_STR, T_ASSIGN, T_LE, T_GE, T_NE, T_DOTDOT,
    K_AND, K_ARRAY, K_BEGIN, K_CASE, K_CONST, K_DIV, K_DO, K_DOWNTO, K_ELSE, K_END, K_FILE, K_FOR, K_FORWARD, K_FUNCTION, K_GOTO, K_IF, K_IN, K_LABEL,
    K_MOD, K_NIL, K_NOT, K_OF, K_OR, K_PACKED, K_PROCEDURE, K_PROGRAM, K_RECORD, K_REPEAT, K_SET, K_THEN, K_TO, K_TYPE, K_UNTIL, K_VAR, K_WHILE, K_WITH,
    K_XOR, K_SHL, K_SHR
};

static char *keywords[] = {
    "and", "array", "begin", "case", "const", "div", "do", "downto", "else", "end", "file", "for", "forward", "function", "goto", "if", "in", "label",
    "mod", "nil", "not", "of", "or", "packed", "procedure", "program", "record", "repeat", "set", "then", "to", "type", "until", "var", "while", "with",
    "xor", "shl", "shr", 0
};

static rfile in;
static wfile ir, sf;           /* the code and the data; the strings */
static char fname[120];
static u16 line = 1;
static int ch;
static u16 tok;
static int tokv;
static char toks[STRMAX + 2];
static u16 toklen;
static u16 pend;               /* a token that was found while reading a number (the .. of 1..5) */

static void cleanup(void);

static void err(const char *msg)
{
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", fname, line, msg);
#else
    char nb[7];
    putstr(fname); putchar(':'); utoa_(line, nb, 10); putstr(nb); putstr(": "); putstr(msg); putchar('\n');
#endif
    cleanup();
    exit(1);
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

static void skip_comment(int close2)       /* after { or (* */
{
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
        tok = T_ID;
        for (k = 0; keywords[k]; k++) if (!strcmp(keywords[k], toks)) { tok = (u16)(K_AND + k); break; }
        return;
    }
    if ((ch >= '0' && ch <= '9') || ch == '$') {
        u16 v = 0;
        if (ch == '$') {
            nextch();
            if (hexval(ch) < 0) err("a hexadecimal number is expected");
            while (hexval(ch) >= 0) { v = (u16)(v * 16 + hexval(ch)); nextch(); }
        } else {
            while (ch >= '0' && ch <= '9') { v = (u16)(v * 10 + ch - '0'); nextch(); }
            if (ch == '.') {
                nextch();
                if (ch == '.') { nextch(); pend = T_DOTDOT; } else pend = '.';
            }
        }
        tokv = (int)v;
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

static void expect(u16 t, const char *msg)
{
    if (tok != t) err(msg);
    next();
}

/* ---------------------------------------------------------------- types */
enum { TY_VOID, TY_INT, TY_CHAR, TY_BOOL, TY_ENUM, TY_SUB, TY_ARRAY, TY_REC, TY_PTR, TY_NIL };

typedef struct {
    u8 k;
    u8 base;            /* sub: the host type; array: the element; pointer: the object */
    u8 ix;              /* array: the index type */
    u8 sg;              /* ordinals of 1 byte: signed */
    u16 size;
    int lo, hi;         /* ordinals: the range; array: the bounds of the index */
    u16 f;              /* record: the first field */
    u8 nf;
} ty_t;

typedef struct { char name[NAMEMAX + 1]; u8 ty; u16 off; } fld_t;

static ty_t tys[NTY];
static u8 nty;
static fld_t flds[NFLD];
static u16 nfld;

#define T_VOID  0
#define T_INT   1
#define T_CHAR  2
#define T_BOOL  3
#define T_NILT  4
#define T_BYTE  5

static u8 new_type(u8 k, u16 size)
{
    if (nty >= NTY) err("too many types");
    tys[nty].k = k;
    tys[nty].size = size;
    tys[nty].base = 0;
    tys[nty].ix = 0;
    tys[nty].sg = 0;
    tys[nty].lo = tys[nty].hi = 0;
    tys[nty].f = 0;
    tys[nty].nf = 0;
    return nty++;
}

static u8 is_ord(u8 t) { u8 k = tys[t].k; return k == TY_INT || k == TY_CHAR || k == TY_BOOL || k == TY_ENUM || k == TY_SUB; }
static u8 is_scalar(u8 t) { return is_ord(t) || tys[t].k == TY_PTR || tys[t].k == TY_NIL; }
static u8 host(u8 t) { while (tys[t].k == TY_SUB) t = tys[t].base; return t; }

/* a subrange lo..hi of the host type h */
static u8 make_sub(u8 h, int lo, int hi)
{
    u8 t = new_type(TY_SUB, 2);
    tys[t].base = h;
    tys[t].lo = lo;
    tys[t].hi = hi;
    if (lo >= 0 && hi <= 255) tys[t].size = 1;
    else if (lo >= -128 && hi <= 127) { tys[t].size = 1; tys[t].sg = 1; }
    return t;
}

/* ---------------------------------------------------------------- symbols */
enum { SK_CONST, SK_TYPE, SK_VAR, SK_PROC, SK_FUNC, SK_STD };
#define F_PARAM  1      /* a value parameter of a type of one word: lives in the argument (or its copy) */
#define F_BYREF  2      /* the slot holds the address */
#define F_COPIED 4      /* a parameter that an inner procedure uses: copied to the frame */
#define F_FWD    8
#define F_COPYIN 16     /* a value parameter of a big type: copied to the frame when the procedure starts */

typedef struct {
    char name[NAMEMAX + 1];
    u8 kind, flags, level, ty;
    int val;            /* a constant; the number of a procedure; a standard routine */
    u16 off;            /* variable: the offset in the frame (or the number of the argument); function: the offset of the result */
    u8 np, pf;          /* procedure: the number of parameters, the first in pars */
} sym_t;

typedef struct { char name[NAMEMAX + 1]; u8 ty, byref; } par_t;

static sym_t syms[NSYM];
static u16 nsym;
static par_t pars[NPAR];
static u16 npar;
static u8 cur_level;             /* the level of the body that is compiled: 0 the program */
static u16 fpv[NLEV + 1], maxfpv[NLEV + 1];
static int active_fn[NLEV + 1];   /* the function whose body is at the level (index in syms), or -1 */
static u16 nproc, nlab, nstr;

static int lookup(const char *n)
{
    int i;
    for (i = (int)nsym - 1; i >= 0; i--) if (!strcmp(syms[i].name, n)) return i;
    return -1;
}

static sym_t *new_sym(const char *n, u8 kind)
{
    sym_t *s;
    int i;
    if (nsym >= NSYM) err("too many names");
    s = &syms[nsym++];
    strcpy(s->name, n);
    s->kind = kind;
    s->flags = 0;
    s->level = cur_level;
    s->ty = 0;
    s->val = 0;
    s->off = 0;
    s->np = 0;
    s->pf = 0;
    return s;
}

/* a name that is declared again in the same block is an error: the symbols of the block are above the mark */
static u16 scope_mark;
static void check_new(const char *n)
{
    u16 i;
    for (i = scope_mark; i < nsym; i++) if (!strcmp(syms[i].name, n)) err("the name is declared twice");
}

/* ---------------------------------------------------------------- output of the IR */
static void put(const char *s) { w_puts(&ir, s); }
static void putn(int v) { w_num(&ir, v); }
static void ins0(const char *op) { put("\t"); put(op); put("\n"); }
static void ins1(const char *op, int n) { put("\t"); put(op); put(" "); putn(n); put("\n"); }
static void insl(const char *op, u16 l) { put("\t"); put(op); put(" L"); putn(l); put("\n"); }
static void deflabel(u16 l) { put("L"); putn(l); put(":\n"); }
static u16 newlab(void) { return ++nlab; }
static void inscall(const char *name, int n) { put("\tcall "); put(name); put(" "); putn(n); put("\n"); }
static void insaddr(const char *name) { put("\taddr "); put(name); put("\n"); }

static void pname(char *d, char pre, u16 n)       /* P<n> L<n> S<n> */
{
    char t[6];
    u8 k = 0, m = 0;
    d[k++] = pre;
    do { t[m++] = (char)('0' + n % 10); n /= 10; } while (n);
    while (m) d[k++] = t[--m];
    d[k] = 0;
}

static void gname(char *d, const char *n)      /* the name of a global variable */
{
    u8 i = 0;
    d[0] = 'v';
    while (n[i] && i < 14) { d[i + 1] = n[i]; i++; }
    d[i + 1] = 0;
}

/* ---------------------------------------------------------------- expression state */
static u8 e_t;      /* the type */
static u8 e_lv;     /* the address of the object is on the stack (a scalar is not loaded yet) */
static u8 e_c;      /* a constant: e_v, nothing is emitted yet */
static int e_v;
static u8 e_word;   /* the object is a word in an argument slot, whatever its type */
static u8 e_str;    /* the value is a string constant (an array of char): the length is e_v */

static void expression(void);
static void statement(void);
static void block(int fs);

static u8 size_of(u8 t) { return (u8)tys[t].size; }

static void emit_load(u8 t, u8 word)
{
    if (!word && tys[t].size == 1 && is_ord(t)) ins0(tys[t].sg || (tys[t].k == TY_SUB && tys[t].lo < 0) ? "load.b" : "load.ub");
    else ins0("load.w");
}

static void rvalue(void)
{
    if (e_c) { ins1("const", e_v); e_c = 0; e_lv = 0; return; }
    if (e_lv && is_scalar(e_t)) {
        emit_load(e_t, e_word);
        e_lv = 0;
    }
    e_word = 0;
}

static void emit_store(u8 t, u8 word)
{
    if (!word && tys[t].size == 1 && is_ord(t)) ins0("store.b"); else ins0("store.w");
}

/* the frame of the body at level lvl */
static void frame_ref(u8 lvl)
{
    u8 h;
    ins1("laddr", 0);
    for (h = (u8)(cur_level - lvl); h; h--) ins0("load.w");
}

static void var_addr(sym_t *s)
{
    e_word = 0;
    if (s->level == 0) {
        char nm[20];
        gname(nm, s->name);
        insaddr(nm);
        return;
    }
    if ((s->flags & F_PARAM) && !(s->flags & F_COPIED) && !(s->flags & F_BYREF)) ins1("paddr", s->off);
    else if ((s->flags & F_BYREF) && !(s->flags & F_COPIED) && (s->flags & F_PARAM)) { ins1("paddr", s->off); ins0("load.w"); }
    else {
        if (s->level == cur_level) ins1("laddr", s->off);
        else {
            frame_ref(s->level);
            if (s->off) { ins1("const", s->off); ins0("add"); }
        }
        if (s->flags & F_BYREF) ins0("load.w");
    }
    if ((s->flags & F_PARAM) && !(s->flags & F_BYREF)) e_word = 1;
}

/* an argument that an inner procedure uses is copied to the frame of the procedure that has it */
static void use_sym(sym_t *s)
{
    if (s->level != 0 && s->level != cur_level && (s->flags & F_PARAM) && !(s->flags & F_COPIED)) {
        s->flags |= F_COPIED;
        s->off = fpv[s->level];       /* (the old value is the number of the argument: kept in val) */
        fpv[s->level] = (u16)(fpv[s->level] + 2);
        if (fpv[s->level] > maxfpv[s->level]) maxfpv[s->level] = fpv[s->level];
    }
}

static u16 new_temp(u8 size)
{
    u16 o = fpv[cur_level];
    fpv[cur_level] = (u16)(o + size);
    if (fpv[cur_level] > maxfpv[cur_level]) maxfpv[cur_level] = fpv[cur_level];
    return o;
}

/* ---------------------------------------------------------------- constants */
static int const_value(u8 *ty);

static int const_value(u8 *ty)           /* [+-] number | char | name */
{
    int neg = 0, v;
    if (tok == '-') { neg = 1; next(); } else if (tok == '+') next();
    if (tok == T_NUM) { v = tokv; *ty = T_INT; next(); }
    else if (tok == T_STR && toklen == 1 && !neg) { v = (u8)toks[0]; *ty = T_CHAR; next(); }
    else if (tok == T_ID) {
        int i = lookup(toks);
        if (i < 0 || syms[i].kind != SK_CONST) err("a constant is expected");
        v = syms[i].val;
        *ty = syms[i].ty;
        next();
    } else err("a constant is expected");
    return neg ? -v : v;
}

/* ---------------------------------------------------------------- type declarations */
static u8 parse_type(void);

typedef struct { u8 t; char name[NAMEMAX + 1]; } fwdptr_t;
static fwdptr_t fwdptrs[6];
static u8 nfwd;

static u8 find_type(const char *n)
{
    int i = lookup(n);
    if (i < 0 || syms[i].kind != SK_TYPE) err("a type name is expected");
    return syms[i].ty;
}

static u8 parse_record(void)
{
    u8 t = new_type(TY_REC, 0);
    u16 off = 0;
    tys[t].f = nfld;
    while (tok != K_END) {
        u16 first = nfld;
        u8 ft;
        u16 n, i;
        for (;;) {
            u16 j;
            if (tok != T_ID) err("a field name is expected");
            for (j = tys[t].f; j < nfld; j++) if (!strcmp(flds[j].name, toks)) err("the field is declared twice");
            if (nfld >= NFLD) err("too many fields");
            strcpy(flds[nfld].name, toks);
            nfld++;
            next();
            if (tok == ',') { next(); continue; }
            break;
        }
        expect(':', "a : is expected");
        ft = parse_type();
        n = nfld;
        for (i = first; i < n; i++) { flds[i].ty = ft; flds[i].off = off; off = (u16)(off + tys[ft].size); }
        if (tok == ';') next(); else break;
    }
    expect(K_END, "end is expected");
    tys[t].size = off;
    tys[t].nf = (u8)(nfld - tys[t].f);
    return t;
}

static u8 parse_type(void)
{
    u8 t;
    if (tok == K_PACKED) next();
    if (tok == '^') {
        next();
        if (tok != T_ID) err("a type name is expected");
        t = new_type(TY_PTR, 2);
        {
            int i = lookup(toks);
            if (i >= 0 && syms[i].kind == SK_TYPE) tys[t].base = syms[i].ty;
            else {
                if (nfwd >= 6) err("too many pointers to types that are not declared yet");
                fwdptrs[nfwd].t = t;
                strcpy(fwdptrs[nfwd].name, toks);
                nfwd++;
            }
        }
        next();
        return t;
    }
    if (tok == K_ARRAY) {
        u8 idx[4], n = 0, i, el;
        next();
        expect('[', "a [ is expected");
        for (;;) {
            if (n >= 4) err("too many dimensions");
            idx[n++] = parse_type();
            if (tok == ',') { next(); continue; }
            break;
        }
        expect(']', "a ] is expected");
        expect(K_OF, "of is expected");
        el = parse_type();
        for (i = n; i > 0; i--) {
            u8 it = idx[i - 1];
            u8 a = new_type(TY_ARRAY, 0);
            long cnt;
            if (!is_ord(it)) err("the index of an array is not an ordinal type");
            tys[a].base = el;
            tys[a].ix = it;
            tys[a].lo = tys[it].k == TY_SUB || tys[it].k == TY_ENUM ? tys[it].lo : (tys[it].k == TY_CHAR ? 0 : tys[it].k == TY_BOOL ? 0 : -32768);
            tys[a].hi = tys[it].k == TY_SUB || tys[it].k == TY_ENUM ? tys[it].hi : (tys[it].k == TY_CHAR ? 255 : tys[it].k == TY_BOOL ? 1 : 32767);
            cnt = (long)tys[a].hi - tys[a].lo + 1;
            if (cnt * (long)tys[el].size > 32767) err("an array is too big");
            tys[a].size = (u16)(cnt * tys[el].size);
            el = a;
        }
        return el;
    }
    if (tok == K_RECORD) { next(); return parse_record(); }
    if (tok == '(') {                      /* an enumeration */
        u8 e = new_type(TY_ENUM, 1);
        int n = 0;
        next();
        for (;;) {
            sym_t *s;
            if (tok != T_ID) err("a name is expected");
            check_new(toks);
            s = new_sym(toks, SK_CONST);
            s->ty = e;
            s->val = n++;
            next();
            if (tok == ',') { next(); continue; }
            break;
        }
        expect(')', "a ) is expected");
        tys[e].lo = 0;
        tys[e].hi = n - 1;
        if (n > 256) tys[e].size = 2;
        return e;
    }
    if (tok == T_ID) {
        int i = lookup(toks);
        if (i >= 0 && syms[i].kind == SK_TYPE) { t = syms[i].ty; next(); return t; }
    }
    {                                      /* a subrange */
        u8 t1, t2;
        int lo = const_value(&t1), hi;
        expect(T_DOTDOT, "a .. is expected");
        hi = const_value(&t2);
        if (hi < lo) err("an empty range");
        if (host(t1) != host(t2)) err("the bounds of a range are of different types");
        return make_sub(host(t1), lo, hi);
    }
}

/* ---------------------------------------------------------------- strings */
static u8 str_type(u16 len)
{
    u8 i, t;
    for (i = 1; i < nty; i++)
        if (tys[i].k == TY_ARRAY && tys[i].base == T_CHAR && tys[i].lo == 1 && tys[i].hi == (int)len && tys[i].ix == T_INT) return i;
    t = new_type(TY_ARRAY, len);
    tys[t].base = T_CHAR;
    tys[t].ix = T_INT;
    tys[t].lo = 1;
    tys[t].hi = (int)len;
    return t;
}

static char strpool[500];
static u16 strlen_pool;

/* the text of a string as data; the address is put on the stack */
static void string_literal(const char *s, u16 len)
{
    char nm[8];
    u16 i;
    nstr++;
    pname(nm, 'S', nstr);
    w_puts(&sf, ".data ");
    w_puts(&sf, nm);
    w_puts(&sf, " local\n.s \"");
    for (i = 0; i < len; i++) {
        u8 c = (u8)s[i];
        if (c == '"' || c == '\\') { w_putc(&sf, '\\'); w_putc(&sf, c); }
        else if (c >= 32 && c < 127) w_putc(&sf, c);
        else {
            w_puts(&sf, "\\x");
            w_putc(&sf, "0123456789ABCDEF"[c >> 4]);
            w_putc(&sf, "0123456789ABCDEF"[c & 15]);
        }
    }
    w_puts(&sf, "\"\n.end\n");
    insaddr(nm);
}

/* ---------------------------------------------------------------- expressions */
#define ST_ABS   1
#define ST_SQR   2
#define ST_ODD   3
#define ST_ORD   4
#define ST_CHR   5
#define ST_SUCC  6
#define ST_PRED  7
#define ST_EOF   8
#define ST_EOLN  9
#define ST_WRITE 20
#define ST_WRITELN 21
#define ST_READ  22
#define ST_READLN 23
#define ST_NEW   24
#define ST_DISPOSE 25
#define ST_HALT  26
#define ST_INC   27
#define ST_DEC   28

static void std_call(int id);
static void call_user(int si);
static void factor(void);
static void expression(void);

static u8 same_type(u8 a, u8 b)
{
    if (a == b) return 1;
    if (is_ord(a) && is_ord(b)) return host(a) == host(b);
    if (tys[a].k == TY_PTR && tys[b].k == TY_NIL) return 1;
    if (tys[a].k == TY_NIL && tys[b].k == TY_PTR) return 1;
    return 0;
}

static void need_ord(void) { if (!is_ord(e_t)) err("an ordinal value is expected"); }

static int find_field(u8 t, const char *n)
{
    u16 i;
    for (i = tys[t].f; i < (u16)(tys[t].f + tys[t].nf); i++) if (!strcmp(flds[i].name, n)) return (int)i;
    return -1;
}

/* with: the record types that are open, and the offsets of the temporaries that hold their addresses */
static u8 with_ty[NWITH];
static u16 with_off[NWITH];
static u8 nwith;

static void selectors(void)
{
    for (;;) {
        if (tok == '[') {
            next();
            for (;;) {
                u8 at = e_t;
                int lo;
                u16 es;
                if (tys[at].k != TY_ARRAY) err("an array is expected");
                lo = tys[at].lo;
                es = tys[tys[at].base].size;
                expression();
                need_ord();
                if (host(e_t) != host(tys[at].ix)) err("the index has the wrong type");
                if (e_c) {
                    int off = (e_v - lo) * (int)es;
                    if (off) { ins1("const", off); ins0("add"); }
                    e_c = 0;
                } else {
                    rvalue();
                    if (lo) { ins1("const", lo); ins0("sub"); }
                    if (es != 1) { ins1("const", es); ins0("mul"); }
                    ins0("add");
                }
                e_t = tys[at].base;
                e_lv = 1;
                e_word = 0;
                if (tok == ',') { next(); continue; }
                break;
            }
            expect(']', "a ] is expected");
        } else if (tok == '.') {
            int f;
            if (tys[e_t].k != TY_REC) err("a record is expected");
            next();
            if (tok != T_ID) err("a field name is expected");
            f = find_field(e_t, toks);
            if (f < 0) err("no such field");
            if (flds[f].off) { ins1("const", flds[f].off); ins0("add"); }
            e_t = flds[f].ty;
            e_lv = 1;
            e_word = 0;
            next();
        } else if (tok == '^') {
            if (tys[e_t].k != TY_PTR) err("a pointer is expected");
            next();
            if (e_lv) emit_load(e_t, e_word);
            e_t = tys[e_t].base;
            e_lv = 1;
            e_word = 0;
        } else break;
    }
}

static void expression_bool(void)
{
    expression();
    rvalue();
    if (host(e_t) != T_BOOL) err("a boolean value is expected");
}

static int fold(u16 op, int a, int b)
{
    switch (op) {
    case '+': return a + b;
    case '-': return a - b;
    case '*': return a * b;
    case K_DIV: if (!b) err("division by zero"); return a / b;
    case K_MOD: if (!b) err("division by zero"); return a % b;
    case K_AND: return a & b;
    case K_OR: return a | b;
    case K_XOR: return a ^ b;
    case K_SHL: return a << b;
    case K_SHR: return a >> b;
    case '=': return a == b;
    case T_NE: return a != b;
    case '<': return a < b;
    case T_LE: return a <= b;
    case '>': return a > b;
    case T_GE: return a >= b;
    }
    return 0;
}

static const char *opname(u16 op)
{
    switch (op) {
    case '+': return "add";
    case '-': return "sub";
    case '*': return "mul";
    case K_DIV: return "div";
    case K_MOD: return "mod";
    case K_AND: return "and";
    case K_OR: return "or";
    case K_XOR: return "xor";
    case K_SHL: return "shl";
    case K_SHR: return "shr";
    case '=': return "eq";
    case T_NE: return "ne";
    case '<': return "lt";
    case T_LE: return "le";
    case '>': return "gt";
    }
    return "ge";
}

/* the right operand is in e_*; the left one is on the stack, or a constant (lc, lv) that is not emitted yet */
static void binary(u16 op, u8 lc, int lv, u8 lt, u8 cmp)
{
    u8 rt = e_t, lb = host(lt), rb = host(rt);
    u8 rest;
    if (cmp) {
        if (!(is_scalar(lt) && is_scalar(rt))) err("these operands cannot be compared");
        if (!same_type(lt, rt)) err("the operands have different types");
        if (tys[lb].k == TY_PTR && op != '=' && op != T_NE) err("pointers can only be tested for equality");
        rest = T_BOOL;
    } else {
        if (!is_ord(lt) || !is_ord(rt)) err("the operands have the wrong types");
        if (op == K_AND || op == K_OR || op == K_XOR) {
            if (lb == T_BOOL && rb == T_BOOL) rest = T_BOOL;
            else if (lb == T_INT && rb == T_INT) rest = T_INT;
            else err("the operands of and/or are not both boolean or both integer");
        } else {
            if (lb != T_INT || rb != T_INT) err("an integer is expected");
            rest = T_INT;
        }
    }
    e_str = 0;
    if (lc && e_c) {
        e_v = fold(op, lv, e_v);
        e_t = rest;
        return;
    }
    if (lc) { rvalue(); ins1("const", lv); ins0("swap"); }
    else if (e_c) { ins1("const", e_v); e_c = 0; }
    else rvalue();
    ins0(opname(op));
    e_c = 0;
    e_lv = 0;
    e_t = rest;
}

static void string_const(sym_t *s)
{
    string_literal(strpool + s->val, tys[s->ty].size);
    e_t = s->ty;
    e_lv = 1;
    e_str = 1;
    e_v = tys[s->ty].size;
}

static void factor(void)
{
    e_c = 0; e_lv = 0; e_word = 0; e_str = 0;
    if (tok == T_NUM) {
        e_c = 1; e_v = tokv; e_t = T_INT;
        next();
    } else if (tok == T_STR) {
        if (toklen == 1) { e_c = 1; e_v = (u8)toks[0]; e_t = T_CHAR; }
        else {
            if (toklen == 0) err("an empty string");
            string_literal(toks, toklen);
            e_t = str_type(toklen);
            e_lv = 1;
            e_str = 1;
            e_v = (int)toklen;
        }
        next();
    } else if (tok == '(') {
        next();
        expression();
        expect(')', "a ) is expected");
    } else if (tok == K_NOT) {
        next();
        factor();
        need_ord();
        if (host(e_t) == T_BOOL) {
            if (e_c) e_v = !e_v; else { rvalue(); ins0("not"); }
        } else if (host(e_t) == T_INT) {
            if (e_c) e_v = ~e_v; else { rvalue(); ins0("com"); }
        } else err("not needs a boolean or an integer");
    } else if (tok == K_NIL) {
        e_c = 1; e_v = 0; e_t = T_NILT;
        next();
    } else if (tok == T_ID) {
        int w, i, f;
        sym_t *s;
        for (w = (int)nwith - 1; w >= 0; w--) {
            f = find_field(with_ty[w], toks);
            if (f >= 0) {
                next();
                ins1("laddr", with_off[w]);
                ins0("load.w");
                if (flds[f].off) { ins1("const", flds[f].off); ins0("add"); }
                e_t = flds[f].ty;
                e_lv = 1;
                selectors();
                return;
            }
        }
        i = lookup(toks);
        if (i < 0) err("undeclared name");
        s = &syms[i];
        switch (s->kind) {
        case SK_CONST:
            if (tys[s->ty].k == TY_ARRAY) string_const(s);
            else { e_c = 1; e_v = s->val; e_t = s->ty; }
            next();
            break;
        case SK_VAR:
            use_sym(s);
            next();
            var_addr(s);
            e_t = s->ty;
            e_lv = 1;
            selectors();
            break;
        case SK_FUNC:
            next();
            call_user(i);
            break;
        case SK_STD:
            next();
            std_call(s->val);
            break;
        default:
            err("a name that is not a value");
        }
    } else err("an expression is expected");
}

static void term(void)
{
    factor();
    for (;;) {
        u16 op = tok;
        u8 lc, lt;
        int lv;
        if (!(op == '*' || op == K_DIV || op == K_MOD || op == K_AND || op == K_SHL || op == K_SHR)) break;
        next();
        need_ord();
        if (op == K_AND && host(e_t) == T_BOOL) {            /* short circuit */
            u16 lf = newlab(), le = newlab();
            rvalue();
            insl("jz", lf);
            factor();
            need_ord();
            if (host(e_t) != T_BOOL) err("a boolean value is expected");
            rvalue();
            insl("jmp", le);
            deflabel(lf);
            ins1("const", 0);
            deflabel(le);
            e_t = T_BOOL;
            continue;
        }
        lc = e_c; lv = e_v; lt = e_t;
        if (!lc) rvalue();
        factor();
        binary(op, lc, lv, lt, 0);
    }
}

static void simple(void)
{
    u8 neg = 0;
    if (tok == '-') { neg = 1; next(); } else if (tok == '+') next();
    term();
    if (neg) {
        need_ord();
        if (host(e_t) != T_INT) err("an integer is expected");
        if (e_c) e_v = -e_v; else { rvalue(); ins0("neg"); }
    }
    for (;;) {
        u16 op = tok;
        u8 lc, lt;
        int lv;
        if (!(op == '+' || op == '-' || op == K_OR || op == K_XOR)) break;
        next();
        need_ord();
        if (op == K_OR && host(e_t) == T_BOOL) {
            u16 lt2 = newlab(), le = newlab();
            rvalue();
            insl("jnz", lt2);
            term();
            need_ord();
            if (host(e_t) != T_BOOL) err("a boolean value is expected");
            rvalue();
            insl("jmp", le);
            deflabel(lt2);
            ins1("const", 1);
            deflabel(le);
            e_t = T_BOOL;
            continue;
        }
        lc = e_c; lv = e_v; lt = e_t;
        if (!lc) rvalue();
        term();
        binary(op, lc, lv, lt, 0);
    }
}

static void expression(void)
{
    u8 lc, lt;
    int lv;
    u16 op;
    simple();
    op = tok;
    if (op == '=' || op == T_NE || op == '<' || op == T_LE || op == '>' || op == T_GE) {
        next();
        lc = e_c; lv = e_v; lt = e_t;
        if (!lc) rvalue();
        simple();
        binary(op, lc, lv, lt, 1);
        e_str = 0;
    }
}

/* ---------------------------------------------------------------- calls */
static void check_assignable(u8 dst)
{
    if (is_scalar(dst)) {
        if (!same_type(dst, e_t)) err("the types do not match");
    } else if (tys[dst].k == TY_ARRAY || tys[dst].k == TY_REC) {
        if (!(e_t == dst || (tys[e_t].k == tys[dst].k && tys[e_t].size == tys[dst].size && !e_str))) err("the types do not match");
    } else err("this type cannot be assigned");
}

static void call_user(int si)
{
    sym_t *s = &syms[si];
    u8 j, total = s->np;
    if (s->level >= 1) { frame_ref(s->level); total++; }
    if (s->np) {
        expect('(', "a ( is expected");
        for (j = 0; j < s->np; j++) {
            par_t *p = &pars[s->pf + j];
            if (j) expect(',', "a , is expected");
            expression();
            if (p->byref) {
                if (!e_lv || e_c) err("a variable is expected for a var parameter");
                if (e_t != p->ty && !(is_ord(e_t) && is_ord(p->ty) && host(e_t) == host(p->ty) && tys[e_t].size == tys[p->ty].size)) err("the types do not match");
                if (e_word) err("a parameter cannot be passed by reference here");
            } else if (is_scalar(p->ty)) {
                check_assignable(p->ty);
                rvalue();
            } else {
                check_assignable(p->ty);
                if (!e_lv) err("a value of this type cannot be passed");
            }
        }
        expect(')', "a ) is expected");
    }
    put("\tcall P");
    putn(s->val);
    put(" ");
    putn(total);
    put("\n");
    e_c = 0; e_lv = 0; e_word = 0; e_str = 0;
    e_t = s->kind == SK_FUNC ? s->ty : T_VOID;
}

static void arg_open(void) { expect('(', "a ( is expected"); }
static void arg_close(void) { expect(')', "a ) is expected"); }

/* the optional  :width  of an item of write; pushes the width (0 when it is not there) */
static void width_arg(void)
{
    if (tok == ':') {
        next();
        expression();
        rvalue();
        if (host(e_t) != T_INT) err("the width must be an integer");
    } else ins1("const", 0);
}

static void write_items(u8 ln)
{
    if (tok == '(') {
        next();
        for (;;) {
            expression();
            if (e_str || (tys[e_t].k == TY_ARRAY && tys[tys[e_t].base].k == TY_CHAR)) {
                ins1("const", tys[e_t].size);
                width_arg();
                inscall("__pws", 3);
            } else if (host(e_t) == T_CHAR) {
                rvalue();
                width_arg();
                inscall("__pwc", 2);
            } else if (host(e_t) == T_BOOL) {
                rvalue();
                width_arg();
                inscall("__pwb", 2);
            } else if (host(e_t) == T_INT) {
                rvalue();
                width_arg();
                inscall("__pwi", 2);
            } else err("this value cannot be written");
            ins0("drop");
            if (tok == ',') { next(); continue; }
            break;
        }
        arg_close();
    }
    if (ln) { inscall("__pwl", 0); ins0("drop"); }
}

static void read_items(u8 ln)
{
    if (tok == '(') {
        next();
        for (;;) {
            u8 t, w;
            expression();
            if (!e_lv || e_c) err("a variable is expected");
            t = e_t;
            w = e_word;
            if (host(t) == T_CHAR) inscall("__prc", 0);
            else if (host(t) == T_INT) inscall("__pri", 0);
            else err("this variable cannot be read");
            emit_store(t, w);
            ins0("drop");
            if (tok == ',') { next(); continue; }
            break;
        }
        arg_close();
    }
    if (ln) { inscall("__prl", 0); ins0("drop"); }
}

static void std_call(int id)
{
    switch (id) {
    case ST_ABS:
        arg_open(); expression(); need_ord();
        if (host(e_t) != T_INT) err("an integer is expected");
        if (e_c) { if (e_v < 0) e_v = -e_v; } else { rvalue(); inscall("abs", 1); }
        e_t = T_INT; arg_close();
        break;
    case ST_SQR:
        arg_open(); expression(); need_ord();
        if (host(e_t) != T_INT) err("an integer is expected");
        if (e_c) e_v = e_v * e_v; else { rvalue(); ins0("dup"); ins0("mul"); }
        e_t = T_INT; arg_close();
        break;
    case ST_ODD:
        arg_open(); expression(); need_ord();
        if (e_c) e_v = e_v & 1; else { rvalue(); ins1("const", 1); ins0("and"); }
        e_t = T_BOOL; arg_close();
        break;
    case ST_ORD:
        arg_open(); expression(); need_ord();
        e_t = T_INT; arg_close();
        break;
    case ST_CHR:
        arg_open(); expression(); need_ord();
        if (!e_c) { rvalue(); ins0("zx"); }
        e_t = T_CHAR; arg_close();
        break;
    case ST_SUCC:
    case ST_PRED:
        arg_open(); expression(); need_ord();
        if (e_c) e_v += id == ST_SUCC ? 1 : -1;
        else { rvalue(); ins1("const", 1); ins0(id == ST_SUCC ? "add" : "sub"); }
        arg_close();
        break;
    case ST_EOF:
    case ST_EOLN:
        inscall(id == ST_EOF ? "__peof" : "__peoln", 0);
        if (tok == '(') { next(); if (tok == T_ID) next(); arg_close(); }
        e_c = 0; e_lv = 0; e_t = T_BOOL;
        break;
    default:
        err("this standard routine cannot be used in an expression");
    }
}

/* ---------------------------------------------------------------- statements */
static void std_stat(int id)
{
    switch (id) {
    case ST_WRITE: write_items(0); break;
    case ST_WRITELN: write_items(1); break;
    case ST_READ: read_items(0); break;
    case ST_READLN: read_items(1); break;
    case ST_NEW: {
        u8 t;
        arg_open();
        expression();
        if (!e_lv || tys[e_t].k != TY_PTR) err("a pointer variable is expected");
        t = e_t;
        ins1("const", tys[tys[t].base].size);
        inscall("malloc", 1);
        ins0("store.w");
        ins0("drop");
        arg_close();
        break;
    }
    case ST_DISPOSE:
        arg_open();
        expression();
        if (tys[e_t].k != TY_PTR) err("a pointer is expected");
        rvalue();
        inscall("free", 1);
        ins0("drop");
        arg_close();
        break;
    case ST_HALT:
        if (tok == '(') { next(); expression(); rvalue(); arg_close(); } else ins1("const", 0);
        inscall("exit", 1);
        ins0("drop");
        break;
    case ST_INC:
    case ST_DEC: {
        u8 t, w;
        arg_open();
        expression();
        if (!e_lv || !is_ord(e_t)) err("an ordinal variable is expected");
        t = e_t; w = e_word;
        ins0("dup");
        emit_load(t, w);
        if (tok == ',') {
            next();
            expression();
            if (host(e_t) != T_INT) err("an integer is expected");
            rvalue();
        } else ins1("const", 1);
        ins0(id == ST_INC ? "add" : "sub");
        emit_store(t, w);
        ins0("drop");
        arg_close();
        break;
    }
    default:
        err("this standard routine is not a statement");
    }
}

static void assignment(void)
{
    u8 t, w;
    expression();
    if (!e_lv || e_c) err("a variable is expected");
    t = e_t;
    w = e_word;
    expect(T_ASSIGN, "a := is expected");
    expression();
    if (e_str && tys[t].k == TY_ARRAY && tys[tys[t].base].k == TY_CHAR) {
        ins1("const", tys[t].size);
        ins1("const", e_v);
        inscall("__pstr", 4);
        ins0("drop");
        return;
    }
    check_assignable(t);
    if (is_scalar(t)) {
        rvalue();
        emit_store(t, w);
        ins0("drop");
    } else {
        if (!e_lv) err("a value of this type cannot be assigned");
        ins1("copy", tys[t].size);
        ins0("drop");
    }
}

/* the result of the function f (a name that is assigned inside the body) */
static void result_assign(int fi)
{
    sym_t *f = &syms[fi];
    u8 lvl = (u8)(f->level + 1);
    u8 h;
    ins1("laddr", 0);
    for (h = (u8)(cur_level - lvl); h; h--) ins0("load.w");
    if (f->off) { ins1("const", f->off); ins0("add"); }
    expect(T_ASSIGN, "a := is expected");
    expression();
    if (!is_scalar(f->ty)) err("a function returns a simple value");
    if (!same_type(f->ty, e_t)) err("the types do not match");
    rvalue();
    emit_store(f->ty, 0);
    ins0("drop");
}

static int active_function(int i)
{
    u8 l;
    for (l = 1; l <= cur_level; l++) if (active_fn[l] == i) return 1;
    return 0;
}

static void cond_jump(const char *op, u16 lab)
{
    expression_bool();
    insl(op, lab);
}

static void stat_list(u16 end1, u16 end2)
{
    for (;;) {
        statement();
        if (tok == ';') { next(); }
        else break;
        if (tok == end1 || tok == end2) break;
    }
}

static void case_statement(void)
{
    u16 t, lend = newlab();
    u16 save = fpv[cur_level];
    next();
    expression();
    need_ord();
    rvalue();
    t = new_temp(2);
    ins1("laddr", t);
    ins0("swap");
    ins0("store.w");
    ins0("drop");
    expect(K_OF, "of is expected");
    while (tok != K_END && tok != K_ELSE) {
        u16 lbody = newlab(), lnext = newlab();
        for (;;) {
            u8 ty;
            int lo = const_value(&ty), hi;
            if (tok == T_DOTDOT) {
                u16 lskip = newlab();
                next();
                hi = const_value(&ty);
                ins1("laddr", t); ins0("load.w"); ins1("const", lo); ins0("lt"); insl("jnz", lskip);
                ins1("laddr", t); ins0("load.w"); ins1("const", hi); ins0("le"); insl("jnz", lbody);
                deflabel(lskip);
            } else {
                ins1("laddr", t); ins0("load.w"); ins1("const", lo); ins0("eq"); insl("jnz", lbody);
            }
            if (tok == ',') { next(); continue; }
            break;
        }
        expect(':', "a : is expected");
        insl("jmp", lnext);
        deflabel(lbody);
        statement();
        insl("jmp", lend);
        deflabel(lnext);
        if (tok == ';') next(); else break;
    }
    if (tok == K_ELSE) {
        next();
        stat_list(K_END, K_END);
    }
    expect(K_END, "end is expected");
    deflabel(lend);
    fpv[cur_level] = save;
}

static void with_statement(void)
{
    u8 n0 = nwith;
    u16 save = fpv[cur_level];
    next();
    for (;;) {
        u16 o;
        expression();
        if (!e_lv || tys[e_t].k != TY_REC) err("a record variable is expected");
        if (nwith >= NWITH) err("with is nested too deep");
        o = new_temp(2);
        ins1("laddr", o);
        ins0("swap");
        ins0("store.w");
        ins0("drop");
        with_ty[nwith] = e_t;
        with_off[nwith] = o;
        nwith++;
        if (tok == ',') { next(); continue; }
        break;
    }
    expect(K_DO, "do is expected");
    statement();
    nwith = n0;
    fpv[cur_level] = save;
}

static void for_statement(void)
{
    int vi;
    sym_t *s;
    u8 down, t, w;
    u16 tm, ltop, lbody, lend = newlab();
    u16 save = fpv[cur_level];
    next();
    if (tok != T_ID) err("a variable is expected");
    vi = lookup(toks);
    if (vi < 0 || syms[vi].kind != SK_VAR || !is_ord(syms[vi].ty)) err("the control variable must be an ordinal variable");
    s = &syms[vi];
    t = s->ty;
    next();
    expect(T_ASSIGN, "a := is expected");
    expression();
    if (!same_type(t, e_t)) err("the types do not match");
    rvalue();
    use_sym(s); var_addr(s); w = e_word;
    ins0("swap"); emit_store(t, w); ins0("drop");
    if (tok == K_TO) down = 0; else if (tok == K_DOWNTO) down = 1; else err("to or downto is expected");
    next();
    expression();
    if (!same_type(t, e_t)) err("the types do not match");
    rvalue();
    tm = new_temp(2);
    ins1("laddr", tm); ins0("swap"); ins0("store.w"); ins0("drop");
    expect(K_DO, "do is expected");
    ltop = newlab(); lbody = newlab();
    (void)ltop;
    var_addr(s); emit_load(t, w); ins1("laddr", tm); ins0("load.w"); ins0(down ? "lt" : "gt"); insl("jnz", lend);
    deflabel(lbody);
    statement();
    var_addr(s); emit_load(t, w); ins1("laddr", tm); ins0("load.w"); ins0("eq"); insl("jnz", lend);
    var_addr(s); ins0("dup"); emit_load(t, w); ins1("const", 1); ins0(down ? "sub" : "add"); emit_store(t, w); ins0("drop");
    insl("jmp", lbody);
    deflabel(lend);
    fpv[cur_level] = save;
}

static void statement(void)
{
    u16 save = fpv[cur_level];
    if (tok == T_NUM) {            /* a label */
        next();
        expect(':', "a : is expected");
    }
    switch (tok) {
    case K_BEGIN:
        next();
        if (tok != K_END) stat_list(K_END, K_END);
        expect(K_END, "end is expected");
        break;
    case K_IF: {
        u16 lelse = newlab(), lend;
        next();
        cond_jump("jz", lelse);
        expect(K_THEN, "then is expected");
        statement();
        if (tok == K_ELSE) {
            lend = newlab();
            next();
            insl("jmp", lend);
            deflabel(lelse);
            statement();
            deflabel(lend);
        } else deflabel(lelse);
        break;
    }
    case K_WHILE: {
        u16 ltop = newlab(), lend = newlab();
        next();
        deflabel(ltop);
        cond_jump("jz", lend);
        expect(K_DO, "do is expected");
        statement();
        insl("jmp", ltop);
        deflabel(lend);
        break;
    }
    case K_REPEAT: {
        u16 ltop = newlab();
        next();
        deflabel(ltop);
        stat_list(K_UNTIL, K_UNTIL);
        expect(K_UNTIL, "until is expected");
        cond_jump("jz", ltop);
        break;
    }
    case K_FOR: for_statement(); break;
    case K_CASE: case_statement(); break;
    case K_WITH: with_statement(); break;
    case T_ID: {
        int i = lookup(toks);
        sym_t *s;
        if (i >= 0) {
            s = &syms[i];
            if (s->kind == SK_PROC) { next(); call_user(i); ins0("drop"); break; }
            if (s->kind == SK_STD && s->val >= ST_WRITE) { next(); std_stat(s->val); break; }
            if (s->kind == SK_FUNC && active_function(i)) {
                char pk[2];
                next();
                if (tok == T_ASSIGN) { result_assign(i); break; }
                (void)pk;
                call_user(i);
                ins0("drop");
                break;
            }
            if (s->kind == SK_FUNC) { next(); call_user(i); ins0("drop"); break; }
        }
        assignment();
        break;
    }
    default:
        break;                       /* the empty statement */
    }
    fpv[cur_level] = save;
}

/* ---------------------------------------------------------------- declarations */
static u16 frame_patch, frame_ph;

static void var_section(void)
{
    next();
    while (tok == T_ID) {
        char names[6][NAMEMAX + 1];
        u8 n = 0, i, t;
        for (;;) {
            if (tok != T_ID) err("a name is expected");
            if (n >= 6) err("too many names in one declaration");
            check_new(toks);
            strcpy(names[n++], toks);
            next();
            if (tok == ',') { next(); continue; }
            break;
        }
        expect(':', "a : is expected");
        t = parse_type();
        for (i = 0; i < n; i++) {
            sym_t *s = new_sym(names[i], SK_VAR);
            s->ty = t;
            if (cur_level == 0) {
                char nm[20];
                gname(nm, names[i]);
                put(".bss "); put(nm); put(" "); putn(tys[t].size); put("\n");
            } else {
                s->off = fpv[cur_level];
                fpv[cur_level] = (u16)(fpv[cur_level] + tys[t].size);
                if (fpv[cur_level] > maxfpv[cur_level]) maxfpv[cur_level] = fpv[cur_level];
            }
        }
        expect(';', "a ; is expected");
    }
}

static void const_section(void)
{
    next();
    while (tok == T_ID) {
        sym_t *s;
        char nm[NAMEMAX + 1];
        u8 t;
        int v;
        check_new(toks);
        strcpy(nm, toks);
        next();
        expect('=', "a = is expected");
        if (tok == T_STR && toklen > 1) {
            if (strlen_pool + toklen + 1 > sizeof(strpool)) err("too many string constants");
            memcpy(strpool + strlen_pool, toks, toklen);
            s = new_sym(nm, SK_CONST);
            s->ty = str_type(toklen);
            s->val = (int)strlen_pool;
            strlen_pool += toklen;
            next();
        } else {
            v = const_value(&t);
            s = new_sym(nm, SK_CONST);
            s->ty = t;
            s->val = v;
        }
        expect(';', "a ; is expected");
    }
}

static void resolve_pointers(void)
{
    u8 i;
    for (i = 0; i < nfwd; i++) {
        int k = lookup(fwdptrs[i].name);
        if (k < 0 || syms[k].kind != SK_TYPE) err("the type of a pointer is not declared");
        tys[fwdptrs[i].t].base = syms[k].ty;
    }
    nfwd = 0;
}

static void type_section(void)
{
    next();
    while (tok == T_ID) {
        sym_t *s;
        char nm[NAMEMAX + 1];
        u8 t;
        check_new(toks);
        strcpy(nm, toks);
        next();
        expect('=', "a = is expected");
        t = parse_type();
        s = new_sym(nm, SK_TYPE);
        s->ty = t;
        expect(';', "a ; is expected");
    }
    resolve_pointers();
}

static void param_list(sym_t *s)
{
    u8 np = 0;
    s->pf = (u8)npar;
    if (tok == '(') {
        next();
        for (;;) {
            u8 byref = 0, first = (u8)npar, i, t;
            if (tok == K_VAR) { byref = 1; next(); }
            for (;;) {
                if (tok != T_ID) err("a name is expected");
                if (npar >= NPAR) err("too many parameters");
                strcpy(pars[npar].name, toks);
                pars[npar].byref = byref;
                npar++;
                next();
                if (tok == ',') { next(); continue; }
                break;
            }
            expect(':', "a : is expected");
            if (tok != T_ID) err("a type name is expected");
            t = find_type(toks);
            next();
            for (i = first; i < npar; i++) pars[i].ty = t;
            if (tok == ';') { next(); continue; }
            break;
        }
        expect(')', "a ) is expected");
    }
    np = (u8)(npar - s->pf);
    s->np = np;
}

static void enter_params(sym_t *f)
{
    u8 j;
    for (j = 0; j < f->np; j++) {
        par_t *p = &pars[f->pf + j];
        u8 slot = (u8)(f->np - 1 - j);
        sym_t *s;
        check_new(p->name);
        s = new_sym(p->name, SK_VAR);
        s->ty = p->ty;
        s->val = slot;
        if (p->byref) { s->flags = F_PARAM | F_BYREF; s->off = slot; }
        else if (is_scalar(p->ty)) { s->flags = F_PARAM; s->off = slot; }
        else {
            s->flags = F_COPYIN;
            s->off = fpv[cur_level];
            fpv[cur_level] = (u16)(fpv[cur_level] + tys[p->ty].size);
            if (fpv[cur_level] > maxfpv[cur_level]) maxfpv[cur_level] = fpv[cur_level];
        }
    }
}

static void proc_decl(u8 isfunc)
{
    int si = -1, i;
    sym_t *s;
    u16 base, oldmark;
    char nm[NAMEMAX + 1];
    next();
    if (tok != T_ID) err("a name is expected");
    strcpy(nm, toks);
    for (i = scope_mark; i < (int)nsym; i++) if (!strcmp(syms[i].name, nm) && (syms[i].flags & F_FWD)) si = i;
    next();
    if (si < 0) {
        check_new(nm);
        s = new_sym(nm, isfunc ? SK_FUNC : SK_PROC);
        si = (int)(s - syms);
        s->val = ++nproc;
        param_list(s);
        if (isfunc) {
            expect(':', "a : is expected");
            if (tok != T_ID) err("a type name is expected");
            s->ty = find_type(toks);
            if (!is_scalar(s->ty)) err("a function returns a simple value");
            next();
        }
    } else {
        s = &syms[si];
        if (tok == '(') { u16 keep = npar; param_list(s); npar = keep; s->np = syms[si].np; }
        if (isfunc && tok == ':') { next(); next(); }
    }
    expect(';', "a ; is expected");
    if (tok == K_FORWARD) {
        next();
        expect(';', "a ; is expected");
        s->flags |= F_FWD;
        return;
    }
    s->flags &= (u8)~F_FWD;
    oldmark = scope_mark;
    base = nsym;
    scope_mark = base;
    cur_level++;
    if (cur_level > NLEV) err("procedures are nested too deep");
    fpv[cur_level] = cur_level >= 2 ? 2 : 0;
    maxfpv[cur_level] = fpv[cur_level];
    active_fn[cur_level] = isfunc ? si : -1;
    enter_params(s);
    block(si);
    nsym = base;
    scope_mark = oldmark;
    cur_level--;
    expect(';', "a ; is expected");
}

static void block(int fs)
{
    u16 base = scope_mark;
    u16 i;
    char nm[8];
    u8 isfunc = fs >= 0 && syms[fs].kind == SK_FUNC;
    u16 res = 0;
    if (isfunc) {
        res = new_temp(tys[syms[fs].ty].size);
        syms[fs].off = res;
    }
    for (;;) {
        if (tok == K_LABEL) {
            next();
            while (tok != ';') next();
            next();
        } else if (tok == K_CONST) const_section();
        else if (tok == K_TYPE) type_section();
        else if (tok == K_VAR) var_section();
        else if (tok == K_PROCEDURE) proc_decl(0);
        else if (tok == K_FUNCTION) proc_decl(1);
        else break;
    }
    for (i = base; i < nsym; i++)
        if (syms[i].flags & F_FWD) err("a forward procedure has no body");
    if (tok != K_BEGIN) err("begin is expected");
    next();
    put(".func ");
    if (fs < 0) put("main"); else { pname(nm, 'P', (u16)syms[fs].val); put(nm); }
    put(" ");
    frame_patch = w_tell(&ir);
    frame_ph = w_tellh(&ir);
    put("00000\n");
    if (cur_level >= 2) {                      /* the static link */
        ins1("laddr", 0);
        ins1("paddr", syms[fs].np);
        ins0("load.w");
        ins0("store.w");
        ins0("drop");
    }
    for (i = base; i < nsym; i++) {
        sym_t *s = &syms[i];
        if (s->kind != SK_VAR) continue;
        if (s->flags & F_COPYIN) {
            ins1("laddr", s->off);
            ins1("paddr", s->val);
            ins0("load.w");
            ins1("copy", tys[s->ty].size);
            ins0("drop");
        } else if (s->flags & F_COPIED) {
            ins1("laddr", s->off);
            ins1("paddr", s->val);
            ins0("load.w");
            ins0("store.w");
            ins0("drop");
        }
    }
    if (tok != K_END) stat_list(K_END, K_END);
    expect(K_END, "end is expected");
    if (isfunc) {
        ins1("laddr", res);
        emit_load(syms[fs].ty, 0);
        ins0("ret");
    } else if (fs < 0) {
        ins1("const", 0);
        ins0("ret");
    }
    put(".end\n");
    {
        char t[6];
        u16 u = maxfpv[cur_level];
        u8 k;
        for (k = 5; k > 0; k--) { t[k - 1] = (char)('0' + u % 10); u /= 10; }
        w_patch(&ir, frame_ph, frame_patch, t, 5);
    }
}

/* ---------------------------------------------------------------- the program */
static void define_std(void)
{
    static char *stds[] = { "abs", "sqr", "odd", "ord", "chr", "succ", "pred", "eof", "eoln", "write", "writeln", "read", "readln", "new", "dispose", "halt", "inc", "dec", 0 };
    static u8 ids[] = { ST_ABS, ST_SQR, ST_ODD, ST_ORD, ST_CHR, ST_SUCC, ST_PRED, ST_EOF, ST_EOLN, ST_WRITE, ST_WRITELN, ST_READ, ST_READLN, ST_NEW, ST_DISPOSE, ST_HALT, ST_INC, ST_DEC };
    u8 i;
    sym_t *s;
    for (i = 0; stds[i]; i++) { s = new_sym(stds[i], SK_STD); s->val = ids[i]; }
    s = new_sym("integer", SK_TYPE); s->ty = T_INT;
    s = new_sym("char", SK_TYPE); s->ty = T_CHAR;
    s = new_sym("boolean", SK_TYPE); s->ty = T_BOOL;
    s = new_sym("byte", SK_TYPE); s->ty = T_BYTE;
    s = new_sym("true", SK_CONST); s->ty = T_BOOL; s->val = 1;
    s = new_sym("false", SK_CONST); s->ty = T_BOOL; s->val = 0;
    s = new_sym("maxint", SK_CONST); s->ty = T_INT; s->val = 32767;
}

static void cleanup(void)
{
    w_close(&ir);
    w_close(&sf);
#ifdef CC_HOST
    remove("pas1s.tmp");
#else
    unlink("pas1s.tmp");
#endif
}

int main(int argc, char **argv)
{
    int i, tail = tail_start(argc, argv);
    const char *src = NULL, *dst = NULL;
    char outname[120];
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < tail) dst = argv[++i];
        else if (argv[i][0] == '-') { puts("pas1: unknown option"); return 2; }
        else src = argv[i];
    }
    if (!src) { puts("usage: pas1 [-o out.ir] file.pas"); return 2; }
    if (!dst) {
        u16 n = (u16)strlen(src);
        if (n > 112) n = 112;
        memcpy(outname, src, n);
        outname[n] = 0;
        {
            u16 k = n;
            while (k > 0 && outname[k - 1] != '.' && outname[k - 1] != '/') k--;
            if (k > 0 && outname[k - 1] == '.') n = (u16)(k - 1);
        }
        outname[n] = 0;
        strcat(outname, ".ir");
        dst = outname;
    }
    strcpy(fname, src);
    if (r_open(&in, src)) { puts("pas1: cannot open the input"); return 2; }
    if (w_open(&ir, dst)) { puts("pas1: cannot create the output"); return 2; }
    if (w_open(&sf, "pas1s.tmp")) { puts("pas1: cannot create pas1s.tmp"); return 2; }
    tys[T_VOID].k = TY_VOID;
    tys[T_INT].k = TY_INT; tys[T_INT].size = 2; tys[T_INT].lo = -32768; tys[T_INT].hi = 32767;
    tys[T_CHAR].k = TY_CHAR; tys[T_CHAR].size = 1; tys[T_CHAR].lo = 0; tys[T_CHAR].hi = 255;
    tys[T_BOOL].k = TY_BOOL; tys[T_BOOL].size = 1; tys[T_BOOL].lo = 0; tys[T_BOOL].hi = 1;
    tys[T_NILT].k = TY_NIL; tys[T_NILT].size = 2;
    tys[T_BYTE].k = TY_SUB; tys[T_BYTE].size = 1; tys[T_BYTE].base = T_INT; tys[T_BYTE].lo = 0; tys[T_BYTE].hi = 255;
    nty = 6;
    define_std();
    scope_mark = nsym;
    line = 1;
    nextch();
    if (ch == '\n') line = 1;
    next();
    if (tok == K_PROGRAM) {
        next();
        if (tok != T_ID) err("the name of the program is expected");
        next();
        if (tok == '(') { while (tok != ')' && tok != T_EOF) next(); next(); }
        expect(';', "a ; is expected");
    }
    cur_level = 0;
    fpv[0] = 0;
    maxfpv[0] = 0;
    active_fn[0] = -1;
    block(-1);
    if (tok != '.') err("a . is expected at the end of the program");
    w_close(&sf);
    r_close(&in);
    {
        rfile r;
        int c;
        if (!r_open(&r, "pas1s.tmp")) {
            while ((c = r_getc(&r)) >= 0) w_putc(&ir, c);
            r_close(&r);
        }
#ifdef CC_HOST
        remove("pas1s.tmp");
#else
        unlink("pas1s.tmp");
#endif
    }
    w_close(&ir);
    return run_tail(argc, argv, tail);
}
