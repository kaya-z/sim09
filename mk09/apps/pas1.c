/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pas1 - the front end of the Pascal compiler of MK09: Pascal in, IR out (docs/IR.md, docs/PASCAL.md)
 *
 *   pas1 -o out.ir file.pas
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
#define NPAR    400
#define NPN     40
#else
#define NSYM    90
#define NTY     60
#define NFLD    56
#define NPAR    120
#define NPN     16
#endif
#define NAMEMAX 12
#define STRMAX  160
#define NLEV    4
#define NWITH   6

#include "pastok.h"

static rfile in;
static wfile ir, sf;           /* the code and the data; the strings */
static char fname[80];
static u16 line = 1;
static u16 tok;
static int tokv;
static u16 tokh;               /* the high word of a number (a constant above 65535 is a longint); of a real: tokv is the high word and tokh the low word */
static char toks[STRMAX + 2];
static u16 toklen;

static void cleanup(void);

/* With MSGS (the build for MK09, see tools/msgsplit.py) a message is a number: line N of /LIB/PAS1.MSG */
#ifdef MSGS
typedef u8 msgt;
#else
typedef const char *msgt;
#endif

#ifdef MSGS
static u8 msgno;                    /* (err is a macro that sets this and calls fail: shorter than a call with an argument) */
static int fail(void)
#else
static void err(msgt msg)
#endif
{
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", fname, line, msg);
#elif defined(MSGS)
    {                                       /* pmsg prints the message (it reads /LIB/PAS1.MSG), in the place of this program */
        char ln[7], mn[4];
        char *av[7];
        cleanup();
        utoa_(line, ln, 10);
        utoa_(msgno, mn, 10);
        av[0] = "pas1"; av[1] = ";"; av[2] = "pmsg"; av[3] = "/LIB/PAS1.MSG"; av[4] = fname; av[5] = ln; av[6] = mn;
        run_tail(7, av, 1);
    }
#else
    printf("%s:%u: %s\n", fname, line, msg);
#endif
    cleanup();
    exit(1);
#ifdef MSGS
    return 0;
#endif
}
#ifdef MSGS
#define err(n) (msgno = (n), fail())
#endif

/* ---------------------------------------------------------------- the tokens (written by pas0) */
static u16 stack_size = 2000, heap_size;       /* the stack and the heap of the program: {$M stack,heap} */

static u16 rw(void) { u16 h = (u16)r_getc(&in); return (u16)((h << 8) | (u16)r_getc(&in)); }

static void next(void)
{
    int t, n, i;
    for (;;) {
        t = r_getc(&in);
        if (t == TK_LINE) line = rw();
        else if (t == TK_STACK) stack_size = rw();
        else if (t == TK_HEAP) heap_size = rw();
        else if (t == TK_FILE) { n = r_getc(&in); for (i = 0; i < n; i++) fname[i] = (char)r_getc(&in); fname[n] = 0; }
        else break;
    }
    if (t < 0) t = T_EOF;
    tok = (u16)t;
    if (t == T_NUM) { tokv = (int)rw(); tokh = rw(); }
    else if (t == T_ID || t == T_STR || t == T_REAL) {
        toklen = (u16)r_getc(&in);
        for (i = 0; i < (int)toklen; i++) toks[i] = (char)r_getc(&in);
        toks[toklen] = 0;
    }
}

static void expect(u16 t, msgt msg)
{
    if (tok != t) {
#ifdef MSGS
        msgno = msg;
        fail();
#else
        err(msg);
#endif
    }
    next();
}

/* ---------------------------------------------------------------- types */
enum { TY_VOID, TY_INT, TY_CHAR, TY_BOOL, TY_ENUM, TY_SUB, TY_ARRAY, TY_REC, TY_PTR, TY_NIL, TY_REAL, TY_LONG, TY_STR, TY_SET, TY_FILE, TY_PROC };

typedef struct {
    u8 ix;              /* array: the index type */
    u8 sg;              /* ordinals of 1 byte: signed */
    int lo, hi;         /* ordinals: the range; array: the bounds of the index */
    u16 f;              /* record: the first field;  procedure: ... */
    u8 nf;
} ty_t;
/* (the fields that are used most are arrays of their own: shorter code than a field of an array of structures) */
static u8 ty_k[NTY];
static u8 ty_base[NTY];    /* sub: the host type; array: the element; pointer: the object */
static u16 ty_size[NTY];

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
#define TYP_REAL  6
#define TYP_LONG 7
#define TYP_TEXT 8
#define TYP_WORD 9
#define TYP_SHORT 10

static u8 new_type(u8 k, u16 size)
{
    if (nty >= NTY) err("too many types");
    ty_k[nty] = k;
    ty_size[nty] = size;
    ty_base[nty] = 0;
    tys[nty].ix = 0;
    tys[nty].sg = 0;
    tys[nty].lo = tys[nty].hi = 0;
    tys[nty].f = 0;
    tys[nty].nf = 0;
    return nty++;
}

static u8 is_ord(u8 t) { u8 k = ty_k[t]; return k == TY_INT || k == TY_CHAR || k == TY_BOOL || k == TY_ENUM || k == TY_SUB; }
static u8 is_scalar(u8 t) { return is_ord(t) || ty_k[t] == TY_PTR || ty_k[t] == TY_NIL || ty_k[t] == TY_PROC; }
static u8 isstr(u8 t) { return ty_k[t] == TY_STR; }
static u8 is_wide(u8 t) { return ty_k[t] == TY_REAL || ty_k[t] == TY_LONG; }
static u8 host(u8 t) { while (ty_k[t] == TY_SUB) t = ty_base[t]; return t; }

/* a subrange lo..hi of the host type h */
static u8 make_sub(u8 h, int lo, int hi)
{
    u8 t = new_type(TY_SUB, 2);
    ty_base[t] = h;
    tys[t].lo = lo;
    tys[t].hi = hi;
    if (lo >= 0 && hi <= 255) ty_size[t] = 1;
    else if (lo >= -128 && hi <= 127) { ty_size[t] = 1; tys[t].sg = 1; }
    return t;
}

/* the standard procedures and functions */
#define ST_ABS   1
#define ST_SQR   2
#define ST_ODD   3
#define ST_ORD   4
#define ST_CHR   5
#define ST_SUCC  6
#define ST_PRED  7
#define ST_EOF   8
#define ST_EOLN  9
#define ST_TRUNC 10
#define ST_ROUND 11
#define ST_SQRT  12
#define ST_SIN   13
#define ST_COS   14
#define ST_ARCTAN 15
#define ST_LN    16
#define ST_EXP   17
#define ST_PI    18
#define ST_INT   19
#define ST_FRAC  20
#define ST_LENGTH 21
#define ST_COPY  22
#define ST_POS   23
#define ST_CONCAT 24
#define ST_UPCASE 25
#define ST_FILEPOS 26
#define ST_FILESIZE 27
#define ST_IORESULT 28
#define ST_WRITE 40
#define ST_WRITELN 41
#define ST_READ  42
#define ST_READLN 43
#define ST_NEW   44
#define ST_DISPOSE 45
#define ST_HALT  46
#define ST_INC   47
#define ST_DEC   48
#define ST_DELETE 49
#define ST_INSERT 50
#define ST_STR   51
#define ST_VAL   52
#define ST_BREAK 53
#define ST_CONTINUE 54
#define ST_EXIT  55
#define ST_INCLUDE 56
#define ST_EXCLUDE 57
#define ST_ASSIGN 58
#define ST_RESET 59
#define ST_REWRITE 60
#define ST_APPEND 61
#define ST_CLOSE 62
#define ST_SEEK 63

/* ---------------------------------------------------------------- symbols */
enum { SK_CONST, SK_TYPE, SK_VAR, SK_PROC, SK_FUNC, SK_STD };
#define F_PARAM  1      /* a value parameter of a type of one word: lives in the argument (or its copy) */
#define F_BYREF  2      /* the slot holds the address */
#define F_COPIED 4      /* a parameter that an inner procedure uses: copied to the frame */
#define F_FWD    8
#define F_STATIC 32     /* a typed constant: a variable in the data (the name T<val>) */
#define F_COPYIN 16     /* a value parameter of a big type: copied to the frame when the procedure starts */

typedef struct {
    char name[NAMEMAX + 1];
    u8 kind, flags, level, ty;
    int val;            /* a constant; the number of a procedure; a standard routine */
    u16 off;            /* variable: the offset in the frame (or the number of the argument); function: the offset of the result */
    u8 np, pf, pn;      /* procedure: the number of parameters, the first in par_ty, the first name in par_name */
} sym_t;

/* the parameters of a procedure: type and var in tables that last; the names only while the procedure is compiled (or until its forward declaration has its body) */
static u8 par_ty[NPAR], par_ref[NPAR];
static char par_name[NPN][NAMEMAX + 1];
static u8 npn;

static sym_t syms[NSYM];
static u16 nsym;
static u16 npar;
static u8 cur_level;             /* the level of the body that is compiled: 0 the program */
static u16 fpv[NLEV + 1], maxfpv[NLEV + 1];
static int active_fn[NLEV + 1];   /* the function whose body is at the level (index in syms), or -1 */
static u16 nproc, nlab, nstr;

static int lookup_user(const char *n)
{
    int i;
    for (i = (int)nsym - 1; i >= 0; i--) if (!strcmp(syms[i].name, n)) return i;
    return -1;
}
static int lookup(const char *n);

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
/* the predefined names do not take a place in the table until they are used */
#define ST_SF    100
#define NSTD 64
static char *pre_names[] = { "abs", "sqr", "odd", "ord", "chr", "succ", "pred", "eof", "eoln", "write", "writeln", "read", "readln", "new", "dispose", "halt", "inc", "dec",
                             "trunc", "round", "sqrt", "sin", "cos", "arctan", "ln", "exp", "pi", "int", "frac",
                             "length", "copy", "pos", "concat", "upcase", "delete", "insert", "str", "val", "break", "continue", "exit", "include", "exclude", "assign", "reset", "rewrite", "append", "close", "seek", "filepos", "filesize", "ioresult",
                             "random", "randomize", "paramcount", "paramstr", "clrscr", "gotoxy", "delay", "readkey", "keypressed", "textcolor", "textbackground", "clreol",
                             "integer", "char", "boolean", "byte", "real", "single", "double", "extended", "longint", "text", "word", "shortint", "smallint", "true", "false", "maxint", 0 };
static u8 pre_val[] = { ST_ABS, ST_SQR, ST_ODD, ST_ORD, ST_CHR, ST_SUCC, ST_PRED, ST_EOF, ST_EOLN, ST_WRITE, ST_WRITELN, ST_READ, ST_READLN, ST_NEW, ST_DISPOSE, ST_HALT, ST_INC, ST_DEC,
                        ST_TRUNC, ST_ROUND, ST_SQRT, ST_SIN, ST_COS, ST_ARCTAN, ST_LN, ST_EXP, ST_PI, ST_INT, ST_FRAC,
                        ST_LENGTH, ST_COPY, ST_POS, ST_CONCAT, ST_UPCASE, ST_DELETE, ST_INSERT, ST_STR, ST_VAL, ST_BREAK, ST_CONTINUE, ST_EXIT, ST_INCLUDE, ST_EXCLUDE, ST_ASSIGN, ST_RESET, ST_REWRITE, ST_APPEND, ST_CLOSE, ST_SEEK, ST_FILEPOS, ST_FILESIZE, ST_IORESULT,
                        ST_SF, ST_SF + 1, ST_SF + 2, ST_SF + 3, ST_SF + 4, ST_SF + 5, ST_SF + 6, ST_SF + 7, ST_SF + 8, ST_SF + 9, ST_SF + 10, ST_SF + 11,
                        T_INT, T_CHAR, T_BOOL, T_BYTE, TYP_REAL, TYP_REAL, TYP_REAL, TYP_REAL, TYP_LONG, TYP_TEXT, TYP_WORD, TYP_SHORT, T_INT, 1, 0, 0 };

static int lookup(const char *n)
{
    int i = lookup_user(n);
    u8 k;
    sym_t *s;
    if (i >= 0) return i;
    for (k = 0; pre_names[k]; k++) if (!strcmp(pre_names[k], n)) break;
    if (!pre_names[k]) return -1;
    if (k < NSTD) { s = new_sym(n, SK_STD); s->val = pre_val[k]; }
    else if (k < NSTD + 13) { s = new_sym(n, SK_TYPE); s->ty = pre_val[k]; }
    else { s = new_sym(n, SK_CONST); s->ty = k == NSTD + 15 ? T_INT : T_BOOL; s->val = k == NSTD + 15 ? 32767 : pre_val[k]; }
    s->level = 0;
    return (int)(s - syms);
}

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
static u8 e_str;    /* the value is a string constant (the address of a string with its length in front): the length is e_v; its type is an array of char */
static u8 e_tmp;    /* the value is a temporary string of 255 characters that can be appended to */
static u8 nest;        /* how deep the parser is (parentheses and statements): the stack of pas1 is small */
static u8 want_proc;   /* the type of the procedural value that the expression is to make (the name of a procedure is its address), or 0 */
static u8 e_called; /* the expression was a call of a procedural variable */
static u8 bin_ltmp; /* the left operand of the binary operator is such a string */
/* a temporary word: stored from the top of the stack, loaded */
static void st_temp(u16 t) { ins1("laddr", t); ins0("swap"); ins0("store.w"); ins0("drop"); }
static void ld_temp(u16 t) { ins1("laddr", t); ins0("load.w"); }

static u16 brk_l[8], cont_l[8];        /* the labels of break and continue of the loops that are open */
static u8 nloop;
static u16 cur_exit;                   /* the end of the function (exit) */

static void loop_open(u16 brk, u16 cont)
{
    if (nloop >= 8) err("loops are nested too deep");
    brk_l[nloop] = brk;
    cont_l[nloop++] = cont;
}


static void expression(void);
static void statement(void);
static void block(int fs);


static void emit_load(u8 t, u8 word)
{
    if (!word && ty_size[t] == 1 && is_ord(t)) ins0(tys[t].sg || (ty_k[t] == TY_SUB && tys[t].lo < 0) ? "load.b" : "load.ub");
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
    if (!word && ty_size[t] == 1 && is_ord(t)) ins0("store.b"); else ins0("store.w");
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
    if (s->flags & F_STATIC) {
        char tn[8];
        pname(tn, 'T', (u16)s->val);
        insaddr(tn);
        return;
    }
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

static u16 new_temp(u16 size)
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
static void param_list(sym_t *s);
static u8 find_type(const char *n);

/* the rest of a procedural type, after the keyword (and the name, in a parameter list): the parameters, and the type of the result of a function */
static u8 proc_type(u8 isf)
{
    sym_t tp;
    u8 t;
    param_list(&tp);
    npn = tp.pn;
    t = new_type(TY_PROC, 2);
    tys[t].f = tp.pf;
    tys[t].nf = tp.np;
    if (isf) {
        expect(':', "a : is expected");
        if (tok != T_ID) err("a type name is expected");
        ty_base[t] = find_type(toks);
        next();
    }
    return t;
}

typedef struct { u8 t; char name[NAMEMAX + 1]; } fwdptr_t;
static fwdptr_t fwdptrs[6];
static u8 nfwd;

static u8 str_ty(int n)             /* string[n]: the length (a byte) and n characters */
{
    u8 i, t;
    for (i = 1; i < nty; i++) if (ty_k[i] == TY_STR && tys[i].hi == n) return i;
    t = new_type(TY_STR, (u16)(n + 1));
    ty_base[t] = T_CHAR;
    tys[t].ix = T_INT;
    tys[t].lo = 0;
    tys[t].hi = n;
    return t;
}

static u8 find_type(const char *n)
{
    if (!strcmp(n, "string")) return str_ty(255);
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
        for (i = first; i < n; i++) { flds[i].ty = ft; flds[i].off = off; off = (u16)(off + ty_size[ft]); }
        if (tok == ';') next(); else break;
    }
    expect(K_END, "end is expected");
    ty_size[t] = off;
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
            if (i >= 0 && syms[i].kind == SK_TYPE) ty_base[t] = syms[i].ty;
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
            ty_base[a] = el;
            tys[a].ix = it;
            tys[a].lo = ty_k[it] == TY_SUB || ty_k[it] == TY_ENUM ? tys[it].lo : (ty_k[it] == TY_CHAR ? 0 : ty_k[it] == TY_BOOL ? 0 : -32768);
            tys[a].hi = ty_k[it] == TY_SUB || ty_k[it] == TY_ENUM ? tys[it].hi : (ty_k[it] == TY_CHAR ? 255 : ty_k[it] == TY_BOOL ? 1 : 32767);
            cnt = (long)tys[a].hi - tys[a].lo + 1;
            if (cnt * (long)ty_size[el] > 32767) err("an array is too big");
            ty_size[a] = (u16)(cnt * ty_size[el]);
            el = a;
        }
        return el;
    }
    if (tok == K_RECORD) { next(); return parse_record(); }
    if (tok == K_PROCEDURE || tok == K_FUNCTION) {
        u8 isf = tok == K_FUNCTION;
        next();
        return proc_type(isf);
    }
    if (tok == K_FILE) {                   /* file of T; a text file has no base type (0) */
        u8 b = 0;
        next();
        if (tok == K_OF) { next(); b = parse_type(); }
        t = new_type(TY_FILE, 2);
        ty_base[t] = b;
        return t;
    }
    if (tok == K_SET) {                    /* a set is 32 bytes whatever its base type is */
        u8 b;
        next();
        expect(K_OF, "of is expected");
        b = parse_type();
        if (!is_ord(b)) err("the base type of a set is an ordinal type");
        t = new_type(TY_SET, 32);
        ty_base[t] = b;
        return t;
    }
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
        if (n > 256) ty_size[e] = 2;
        return e;
    }
    if (tok == T_ID && !strcmp(toks, "string")) {
        int n = 255;
        u8 ty;
        next();
        if (tok == '[') {
            next();
            n = const_value(&ty);
            if (n < 1 || n > 255) err("the length of a string is 1 to 255");
            expect(']', "a ] is expected");
        }
        return str_ty(n);
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
static char strpool[200];
static u16 strlen_pool;

/* the text of a string as data; the address is put on the stack */
static void put_s(const char *s, u16 len);
static void string_literal(const char *s, u16 len, u8 pre)
{
    char nm[8];
    nstr++;
    pname(nm, 'S', nstr);
    w_puts(&sf, ".data ");
    w_puts(&sf, nm);
    w_puts(&sf, " local\n");
    if (pre) { w_puts(&sf, ".b "); w_num(&sf, (int)len); w_putc(&sf, '\n'); }
    put_s(s, len);
    w_puts(&sf, ".b 0\n.end\n");
    insaddr(nm);
}
static void put_s(const char *s, u16 len)
{
    u16 i;
    w_puts(&sf, ".s \"");
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
    w_puts(&sf, "\"\n");
}

/* a real constant: the text is converted when the program runs */
static void real_text(const char *t, u16 len)
{
    string_literal(t, len, 0);
    inscall("atof", 1);
}

/* ---------------------------------------------------------------- expressions */

/* ---------------------------------------------------------------- longint and real: objects of 4 bytes (hi word first) in memory, like long and float of the C compiler
 * (docs/IR.md): a value is always the address of the object (e_lv = 1): a variable or a temporary of the frame that holds a result.  The operations are calls of
 * the library: __fadd(d, b, a) is  *d = *a + *b  (the stack holds a, b, d: d is the top). */
enum { LH_ITOL, LH_ADD, LH_SUB, LH_MUL, LH_DIV, LH_MOD, LH_AND, LH_OR, LH_XOR, LH_SHL, LH_SHR, LH_NEG, LH_CMP, LH_RET,
       LH_ITOF, LH_LTOF, LH_FTOL, LH_FTOI, LH_FADD, LH_FSUB, LH_FMUL, LH_FDIV, LH_FNEG, LH_FCMP, LH_N };
static char *lh_name[LH_N] = { "__itol", "__ladd", "__lsub", "__lmul", "__ldiv", "__lmod", "__land", "__lor", "__lxor", "__lshl", "__lshr", "__lneg", "__lcmp", "__lret",
                               "__itof", "__ltof", "__ftol", "__ftoi", "__fadd", "__fsub", "__fmul", "__fdiv", "__fneg", "__fcmp" };
static u8 lh_used[LH_N];

static void lcall(u8 id, u8 n)
{
    lh_used[id] = 1;
    put("\tcall ");
    put(lh_name[id]);
    put(" ");
    putn(n);
    put("\n");
}

static void wide_literal(u16 hi, u16 lo)
{
    char nm[8];
    nstr++;
    pname(nm, 'S', nstr);
    w_puts(&sf, ".data ");
    w_puts(&sf, nm);
    w_puts(&sf, " local\n.w ");
    w_num(&sf, (int)hi);
    w_putc(&sf, ',');
    w_num(&sf, (int)lo);
    w_puts(&sf, "\n.end\n");
    insaddr(nm);
}

static void wide_to_int(void)          /* the low word of the longint whose address is on the stack */
{
    ins1("const", 2);
    ins0("add");
    ins0("load.w");
    e_t = T_INT;
    e_lv = 0;
}

/* an integer (a word or a constant) or a longint: e_* is made an integer value */
static void narrow(void)
{
    if (ty_k[e_t] == TY_LONG) { wide_to_int(); }
    else if (ty_k[e_t] == TY_REAL) err("a real where an integer is expected (use trunc or round)");
}

/* e_* is a number: an object of the type tt (longint or real) is made of it, in a temporary, if it is not one */
static void to_wide(u8 tt)
{
    u8 from, id;
    if (ty_k[e_t] == ty_k[tt]) return;
    from = ty_k[e_t];
    if (from == TY_REAL) {
        if (ty_k[tt] != TY_LONG) return;
        id = LH_FTOL;
    } else if (from == TY_LONG) id = LH_LTOF;
    else {
        if (e_c) { ins1("const", e_v); e_c = 0; } else rvalue();
        id = ty_k[tt] == TY_REAL ? LH_ITOF : LH_ITOL;
    }
    {
        u16 off = new_temp(4);
        ins1("laddr", off);
        lcall(id, 2);
        ins0("drop");
        ins1("laddr", off);
    }
    e_t = tt;
    e_lv = 1;
    e_c = 0;
}

static u8 is_cmpop(u16 op) { return op == '=' || op == T_NE || op == '<' || op == T_LE || op == '>' || op == T_GE; }

static const char *cmpname(u16 op)
{
    switch (op) {
    case '=': return "eq";
    case T_NE: return "ne";
    case '<': return "lt";
    case T_LE: return "le";
    case '>': return "gt";
    }
    return "ge";
}

/* the left operand is a constant (lc, lv) or on the stack (type lt); the right one is in e_* (an rvalue); at least one is a longint or a real, or the operator is / */
static void wide_binary(u16 op, u8 lc, int lv, u8 lt)
{
    u8 rt = e_t, real, tt, cmp = is_cmpop(op), shift = (op == K_SHL || op == K_SHR), h;
    u16 t;
    real = ty_k[lt] == TY_REAL || ty_k[rt] == TY_REAL || op == '/';
    if (op == K_AND || op == K_OR || op == K_XOR || op == K_DIV || op == K_MOD || shift) {
        if (real) err("an integer is expected");
    }
    if (shift) {
        if (ty_k[rt] == TY_LONG) wide_to_int();
        else if (e_c) { ins1("const", e_v); e_c = 0; } else rvalue();
        if (ty_k[lt] != TY_LONG) err("only a longint can be shifted by a count that is a longint");
        t = new_temp(4);
        ins1("laddr", t);
        lcall(op == K_SHL ? LH_SHL : LH_SHR, 3);
        ins0("drop");
        ins1("laddr", t);
        e_t = TYP_LONG; e_lv = 1; e_c = 0;
        return;
    }
    tt = real ? TYP_REAL : TYP_LONG;
    to_wide(tt);                        /* the right operand */
    if (lc || ty_k[lt] != ty_k[tt]) {   /* the left operand: a constant that is not emitted, or a value or the address of the other kind, under the address of the right one */
        if (lc) ins1("const", lv); else ins0("swap");
        t = new_temp(4);
        ins1("laddr", t);
        lcall(ty_k[lt] == TY_LONG ? LH_LTOF : tt == TYP_REAL ? LH_ITOF : LH_ITOL, 2);
        ins0("drop");
        ins1("laddr", t);
        ins0("swap");
    }
    if (cmp) {
        lcall(real ? LH_FCMP : LH_CMP, 2);
        ins1("const", 0);
        ins0(cmpname(op));
        e_t = T_BOOL; e_lv = 0; e_c = 0;
        return;
    }
    switch (op) {
    case '+': h = real ? LH_FADD : LH_ADD; break;
    case '-': h = real ? LH_FSUB : LH_SUB; break;
    case '*': h = real ? LH_FMUL : LH_MUL; break;
    case '/': h = LH_FDIV; break;
    case K_DIV: h = LH_DIV; break;
    case K_MOD: h = LH_MOD; break;
    case K_AND: h = LH_AND; break;
    case K_OR: h = LH_OR; break;
    default: h = LH_XOR; break;
    }
    t = new_temp(4);
    ins1("laddr", t);
    lcall(h, 3);
    ins0("drop");
    ins1("laddr", t);
    e_t = tt; e_lv = 1; e_c = 0;
}

/* ---------------------------------------------------------------- strings (Turbo Pascal): the length in a byte, then the characters.  A literal is such a string too
 * (its type is an array of char with the index type integer); a value is the address of the string.  The runtime is in clib.c (__p...). */

/* the char value in e_* becomes a string of one character in a temporary */
static void char_to_str(void)
{
    u16 t = new_temp(2);
    rvalue();
    ins1("laddr", t);
    inscall("__pc2s", 2);
    e_t = str_ty(255);
    e_lv = 1;
    e_tmp = 0;
}

static void str_value(void)             /* e_* is made a string (an address) */
{
    if (host(e_t) == T_CHAR && !isstr(e_t)) char_to_str();
    else if (!isstr(e_t)) err("a string is expected");
}

/* the right operand is in e_*; the left one is on the stack (type lt), or a char constant (lc, lv) */
static void str_binary(u16 op, u8 lc, int lv, u8 lt, u8 cmp)
{
    u8 ltmp = bin_ltmp;
    u16 t;
    if (!cmp && op != '+') err("bad operands for a string");
    str_value();
    if (!isstr(lt)) {
        if (host(lt) != T_CHAR) err("a string is expected");
        if (lc) ins1("const", lv); else ins0("swap");
        t = new_temp(2);
        ins1("laddr", t);
        inscall("__pc2s", 2);
        ins0("swap");
        ltmp = 0;
    }
    e_str = 0;
    if (cmp) {
        inscall("__pscmp", 2);
        ins1("const", 0);
        ins0(cmpname(op));
        e_t = T_BOOL; e_lv = 0; e_tmp = 0;
        return;
    }
    if (ltmp) inscall("__pcat", 2);
    else {
        t = new_temp(256);
        ins1("laddr", t);
        inscall("__pcat3", 3);
    }
    e_t = str_ty(255);
    e_lv = 1;
    e_tmp = 1;
}

/* sets: 32 bytes, the bit e of byte e/8; the type of a constructor is a set of 0..255 */
static u8 set_ty(void)
{
    u8 i, t;
    for (i = 1; i < nty; i++) if (ty_k[i] == TY_SET && ty_base[i] == T_BYTE) return i;
    t = new_type(TY_SET, 32);
    ty_base[t] = T_BYTE;
    return t;
}

/* the operands are on the stack: the left one, the right one (addresses of sets) */
static void set_binary(u16 op)
{
    u16 t;
    if (ty_k[e_t] != TY_SET) err("a set is expected");
    if (op == '+' || op == '-' || op == '*') {
        t = new_temp(32);
        ins1("laddr", t);
        inscall(op == '+' ? "__psetu" : op == '-' ? "__psetd" : "__pseti", 3);
        ins0("drop");
        ins1("laddr", t);
        e_t = set_ty();
        e_lv = 1;
        return;
    }
    if (op == T_GE) ins0("swap");
    inscall(op == '=' || op == T_NE ? "__pseteq" : "__psetsub", 2);
    if (op == T_NE) ins0("not");
    e_t = T_BOOL;
    e_lv = 0;
}

static void std_call(int id);
static void call_user(int si);
static void call_var(void);
static void lret_to_temp(u8 ty);
static void factor(void);
static void expression(void);

static u8 same_type(u8 a, u8 b)
{
    if (a == b) return 1;
    if (is_ord(a) && is_ord(b)) return host(a) == host(b);
    if (ty_k[a] == TY_PTR && ty_k[b] == TY_NIL) return 1;
    if (ty_k[a] == TY_NIL && ty_k[b] == TY_PTR) return 1;
    return 0;
}

static void need_ord(void) { if (!is_ord(e_t)) err("an ordinal value is expected"); }
static void need_val(void) { if (!is_ord(e_t) && !is_wide(e_t) && !isstr(e_t) && ty_k[e_t] != TY_SET) err("a value is expected"); }

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
                if (ty_k[at] != TY_ARRAY && ty_k[at] != TY_STR) err("an array is expected");
                lo = tys[at].lo;
                es = ty_size[ty_base[at]];
                expression();
                narrow();
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
                e_t = ty_base[at];
                e_lv = 1;
                e_word = 0;
                if (tok == ',') { next(); continue; }
                break;
            }
            expect(']', "a ] is expected");
        } else if (tok == '.') {
            int f;
            if (ty_k[e_t] != TY_REC) err("a record is expected");
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
            if (ty_k[e_t] != TY_PTR) err("a pointer is expected");
            next();
            if (e_lv) emit_load(e_t, e_word);
            e_t = ty_base[e_t];
            e_lv = 1;
            e_word = 0;
        } else break;
    }
    if (ty_k[e_t] == TY_PROC && tok == '(') call_var();
}

static void expression_bool(void)
{
    expression();
    rvalue();
    if (host(e_t) != T_BOOL) err("a boolean value is expected");
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
    if (ty_k[lt] == TY_SET) { set_binary(op); return; }
    if (isstr(lt) || isstr(rt) || (op == '+' && lb == T_CHAR && rb == T_CHAR)) { str_binary(op, lc, lv, lt, cmp); return; }
    if (is_wide(lt) || is_wide(rt) || op == '/') {
        if (!(is_wide(lt) || (is_ord(lt) && lb == T_INT)) || !(is_wide(rt) || (is_ord(rt) && rb == T_INT))) err("a number is expected");
        wide_binary(op, lc, lv, lt);
        e_str = 0;
        return;
    }
    if (cmp) {
        if (!(is_scalar(lt) && is_scalar(rt))) err("these operands cannot be compared");
        if (!same_type(lt, rt)) err("the operands have different types");
        if (ty_k[lb] == TY_PTR && op != '=' && op != T_NE) err("pointers can only be tested for equality");
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
    if (lc && e_c) { ins1("const", lv); ins1("const", e_v); e_c = 0; }
    else if (lc) { rvalue(); ins1("const", lv); ins0("swap"); }
    else if (e_c) { ins1("const", e_v); e_c = 0; }
    else rvalue();
    ins0(opname(op));
    e_c = 0;
    e_lv = 0;
    e_t = rest;
}

static void string_const(sym_t *s)
{
    string_literal(strpool + s->val, tys[s->ty].hi, 1);
    e_t = s->ty;
    e_lv = 1;
    e_str = 1;
    e_v = tys[s->ty].hi;
}

static void factor(void)
{
    u8 wp;
    e_c = 0; e_lv = 0; e_word = 0; e_str = 0; e_tmp = 0; e_called = 0;
    wp = want_proc;
    want_proc = 0;
    if (tok == T_NUM) {
        if (tokh) { wide_literal(tokh, (u16)tokv); e_t = TYP_LONG; e_lv = 1; }
        else { e_c = 1; e_v = tokv; e_t = T_INT; }
        next();
    } else if (tok == T_REAL) {
        real_text(toks, toklen);
        lret_to_temp(TYP_REAL);
        next();
    } else if (tok == T_STR) {
        if (toklen == 1) { e_c = 1; e_v = (u8)toks[0]; e_t = T_CHAR; }
        else {
            string_literal(toks, toklen, 1);
            e_t = str_ty(toklen);
            e_lv = 1;
            e_str = 1;
            e_v = (int)toklen;
        }
        next();
    } else if (tok == '(') {
        next();
        nest += 11;                           /* (about 5 bytes of stack for each unit) */
        if (nest > 78) err("the expression is nested too deep");
        expression();
        nest -= 11;
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
    } else if (tok == '[') {                    /* a set: a temporary of 32 bytes */
        u16 t = new_temp(32);
        next();
        ins1("laddr", t);
        inscall("__pset0", 1);
        ins0("drop");
        while (tok != ']') {
            expression();
            narrow();
            need_ord();
            rvalue();
            if (tok == T_DOTDOT) {
                next();
                expression();
                need_ord();
                rvalue();
                ins1("laddr", t);
                inscall("__psetr", 3);
            } else {
                ins1("laddr", t);
                inscall("__pseta", 2);
            }
            ins0("drop");
            if (tok == ',') next(); else break;
        }
        expect(']', "a ] is expected");
        ins1("laddr", t);
        e_t = set_ty();
        e_lv = 1;
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
            if (ty_k[s->ty] == TY_STR) string_const(s);
            else if (s->ty == TYP_REAL) { real_text(strpool + s->val, s->off); lret_to_temp(TYP_REAL); }
            else if (is_wide(s->ty)) { wide_literal((u16)s->val, s->off); e_t = s->ty; e_lv = 1; }
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
        case SK_PROC:
            next();
            if (wp && tok != '(') {                 /* the procedure itself, as a value */
                char nm[8];
                if (s->level != 0) err("only a procedure of the program can be a value");
                pname(nm, 'P', (u16)s->val);
                insaddr(nm);
                e_t = wp;
            } else if (s->kind == SK_PROC) err("a procedure in an expression");
            else call_user(i);
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
        u8 lc, lt, lm;
        int lv;
        if (!(op == '*' || op == '/' || op == K_DIV || op == K_MOD || op == K_AND || op == K_SHL || op == K_SHR)) break;
        next();
        need_val();
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
        lc = e_c; lv = e_v; lt = e_t; lm = e_tmp;
        if (!lc) rvalue();
        factor();
        bin_ltmp = lm;
        binary(op, lc, lv, lt, 0);
    }
}

static void simple(void)
{
    u8 neg = 0;
    if (tok == '-') { neg = 1; next(); } else if (tok == '+') next();
    term();
    if (neg) {
        need_val();
        if (is_wide(e_t)) {
            u16 t = new_temp(4);
            ins1("laddr", t);
            lcall(ty_k[e_t] == TY_REAL ? LH_FNEG : LH_NEG, 2);
            ins0("drop");
            ins1("laddr", t);
        } else {
            if (host(e_t) != T_INT) err("an integer is expected");
            if (e_c) e_v = -e_v; else { rvalue(); ins0("neg"); }
        }
    }
    for (;;) {
        u16 op = tok;
        u8 lc, lt, lm;
        int lv;
        if (!(op == '+' || op == '-' || op == K_OR || op == K_XOR)) break;
        next();
        need_val();
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
        lc = e_c; lv = e_v; lt = e_t; lm = e_tmp;
        if (!lc) rvalue();
        term();
        bin_ltmp = lm;
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
    if (op == K_IN) {
        need_ord();
        next();
        lc = e_c; lv = e_v;
        if (!lc) rvalue();
        simple();
        if (ty_k[e_t] != TY_SET) err("a set is expected");
        if (lc) { ins1("const", lv); ins0("swap"); }
        inscall("__pin", 2);
        e_t = T_BOOL; e_lv = 0; e_c = 0;
        return;
    }
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
/* e_* is the value that is stored in an object of the type dst, or passed to a parameter of that type: it is converted; a scalar is on the stack then, a big object
 * is an address */
static void coerce(u8 dst)
{
    if (is_wide(dst)) {
        if (!is_wide(e_t) && !(is_ord(e_t) && host(e_t) == T_INT)) err("the types do not match");
        if (ty_k[dst] == TY_LONG && ty_k[e_t] == TY_REAL) err("a real cannot be given to a longint (use trunc or round)");
        to_wide(dst);
    } else if (is_scalar(dst)) {
        if (ty_k[e_t] == TY_LONG && host(dst) == T_INT) wide_to_int();
        if (!same_type(dst, e_t)) err("the types do not match");
        rvalue();
    } else if (ty_k[dst] == TY_SET) {
        if (ty_k[e_t] != TY_SET) err("the types do not match");
    } else if (ty_k[dst] == TY_STR) {
        str_value();
    } else if (ty_k[dst] == TY_ARRAY || ty_k[dst] == TY_REC) {
        if (!(e_t == dst || (ty_k[e_t] == ty_k[dst] && ty_size[e_t] == ty_size[dst] && !e_str))) err("the types do not match");
    } else err("this type cannot be assigned");
}

/* the result of a library function or of a function that returns a longint or a real is in __lret: it is copied to a temporary (the call has pushed its result: dropped) */
static void lret_to_temp(u8 ty)
{
    u16 sz = ty_size[ty], t = new_temp(sz);
    ins0("drop");
    ins1("laddr", t);
    insaddr(sz == 4 ? "__lret" : "__sret");
    ins1("copy", sz);
    lh_used[LH_RET] = 1;
    e_t = ty;
    e_lv = 1;
    e_c = 0;
    e_tmp = ty_k[ty] == TY_STR;
}

/* the arguments of a call, written as they are read (the first first); np parameters, the first one in par_ty[pf] */
static void call_args(u8 pf, u8 np)
{
    u8 j;
    if (np) {
        expect('(', "a ( is expected");
        for (j = 0; j < np; j++) {
            u8 pt = par_ty[pf + j];
            if (j) expect(',', "a , is expected");
            want_proc = ty_k[pt] == TY_PROC ? pt : 0;
            expression();
            if (par_ref[pf + j]) {
                if (!e_lv || e_c) err("a variable is expected for a var parameter");
                if (e_t != pt && !(is_ord(e_t) && is_ord(pt) && host(e_t) == host(pt) && ty_size[e_t] == ty_size[pt])) err("the types do not match");
                if (e_word) err("a parameter cannot be passed by reference here");
            } else {
                coerce(pt);
                if (!is_scalar(pt) && !e_lv) err("a value of this type cannot be passed");
            }
        }
        expect(')', "a ) is expected");
    } else if (tok == '(') { next(); expect(')', "a ) is expected"); }
}

static void call_user(int si)
{
    sym_t *s = &syms[si];
    u8 total = s->np;
    if (s->level >= 1) { frame_ref(s->level); total++; }
    call_args(s->pf, s->np);
    put("\tcall P");
    putn(s->val);
    put(" ");
    putn(total);
    put("\n");
    e_c = 0; e_lv = 0; e_word = 0; e_str = 0;
    e_t = s->kind == SK_FUNC ? s->ty : T_VOID;
    if (s->kind == SK_FUNC && !is_scalar(s->ty)) lret_to_temp(s->ty);
}

/* e_* is a variable of a procedural type (its address is on the stack): it is called; the pointer goes first, under the arguments */
static void call_var(void)
{
    u8 t = e_t;
    ins0("load.w");
    call_args((u8)tys[t].f, tys[t].nf);
    put("\tcalli ");
    putn(tys[t].nf);
    put("\n");
    e_c = 0; e_lv = 0; e_word = 0; e_str = 0;
    e_t = ty_base[t];
    if (e_t && !is_scalar(e_t)) lret_to_temp(e_t);
    e_called = 1;
}

static void arg_open(void) { expect('(', "a ( is expected"); }
static void arg_close(void) { expect(')', "a ) is expected"); }

/* the optional  :width  of an item of write; pushes the width (0 when it is not there) */
static void width_arg(void)
{
    if (tok == ':') {
        next();
        expression();
        narrow();
        rvalue();
        if (host(e_t) != T_INT) err("the width must be an integer");
    } else ins1("const", 0);
}

/* a file in e_*: its value (the pointer to the record of the runtime) is put on the stack; returns the size of an element (0 for a text) */
static u8 file_value(void)
{
    if (ty_k[e_t] != TY_FILE) err("a file is expected");
    ins0("load.w");
    return ty_base[e_t] ? (u8)ty_size[ty_base[e_t]] : 0;
}

static void skip_std_file(const char *n)       /* output or input as the first argument is the console anyway */
{
    if (tok == T_ID && !strcmp(toks, n)) { next(); if (tok == ',') next(); }
}

static void write_items(u8 ln)
{
    u8 fl = 0, es = 0, first = 1, fb = 0;
    if (tok == '(') {
        next();
        skip_std_file("output");
        for (;;) {
            if (tok == ')') break;
            expression();
            if (first && ty_k[e_t] == TY_FILE) {          /* write(f, ...): f is the file to write to until the end of the statement */
                fl = 1;
                fb = ty_base[e_t];
                es = file_value();
                inscall("__psetout", 1);
                ins0("drop");
                first = 0;
                if (tok == ',') { next(); continue; }
                break;
            }
            first = 0;
            if (es) {                                      /* an element of a file of T: its bytes are written */
                coerce(fb);
                if (is_scalar(fb)) {
                    u16 t = new_temp(2);
                    ins1("laddr", t);
                    ins0("swap");
                    emit_store(fb, 0);
                    ins0("drop");
                    ins1("laddr", t);
                }
                ins1("const", es);
                inscall("__pfwr", 2);
            } else if (isstr(e_t)) {
                width_arg();
                inscall("__pwstr", 2);
            } else if (ty_k[e_t] == TY_ARRAY && ty_k[ty_base[e_t]] == TY_CHAR) {
                ins1("const", ty_size[e_t]);
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
            } else if (ty_k[e_t] == TY_REAL) {
                width_arg();
                if (tok == ':') {
                    next();
                    expression();
                    narrow();
                    rvalue();
                } else ins1("const", -1);
                inscall("__pwr", 3);
            } else if (ty_k[e_t] == TY_LONG) {
                width_arg();
                inscall("__pwg", 2);
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
    if (fl) { ins1("const", 0); inscall("__psetout", 1); ins0("drop"); }
}

static void read_items(u8 ln)
{
    u8 fl = 0, es = 0, first = 1, fb = 0;
    if (tok == '(') {
        next();
        skip_std_file("input");
        for (;;) {
            u8 t, w;
            if (tok == ')') break;
            expression();
            if (first && ty_k[e_t] == TY_FILE) {
                fl = 1;
                fb = ty_base[e_t];
                es = file_value();
                inscall("__psetin", 1);
                ins0("drop");
                first = 0;
                if (tok == ',') { next(); continue; }
                break;
            }
            first = 0;
            if (!e_lv || e_c) err("a variable is expected");
            t = e_t;
            w = e_word;
            if (es) {
                if (!same_type(fb, t)) err("the types do not match");
                ins1("const", es);
                inscall("__pfrd", 2);
                ins0("drop");
            } else if (ty_k[t] == TY_STR) { ins1("const", tys[t].hi); inscall("__prstr", 2); ins0("drop"); }
            else if (ty_k[t] == TY_REAL) { inscall("__prr", 1); ins0("drop"); }
            else if (ty_k[t] == TY_LONG) { inscall("__prg", 1); ins0("drop"); }
            else {
                if (host(t) == T_CHAR) inscall("__prc", 0);
                else if (host(t) == T_INT) inscall("__pri", 0);
                else err("this variable cannot be read");
                emit_store(t, w);
                ins0("drop");
            }
            if (tok == ',') { next(); continue; }
            break;
        }
        arg_close();
    }
    if (ln) { inscall("__prl", 0); ins0("drop"); }
    if (fl) { ins1("const", 0); inscall("__psetin", 1); ins0("drop"); }
}

/* a library function of one argument that takes the address of a real (the argument is converted) and returns a real or a longint in __lret */
static void real_fn(const char *name, u8 result)
{
    arg_open();
    expression();
    need_val();
    to_wide(TYP_REAL);
    inscall(name, 1);
    lret_to_temp(result);
    arg_close();
}

static void str_arg(void)
{
    expression();
    str_value();
}

static void int_arg(void)
{
    expression();
    narrow();
    rvalue();
    if (host(e_t) != T_INT) err("an integer is expected");
}

/* the simple routines of the runtime (__p + the name): integer arguments, and an integer, char, boolean or string result */
static u8 sf_args[] = { 1, 0, 0, 1, 0, 2, 1, 0, 0, 1, 1, 0 };
static char sf_res[] = "i-is---cb---";

static void simple_call(u8 k, u8 stmt)
{
    char nm[24];
    u8 i, r = (u8)sf_res[k];
    strcpy(nm, "__p");
    strcat(nm, pre_names[NSTD - 12 + k]);
    if (sf_args[k]) {
        arg_open();
        for (i = 0; i < sf_args[k]; i++) {
            if (i) expect(',', "a , is expected");
            int_arg();
        }
        arg_close();
    } else if (tok == '(') { next(); arg_close(); }
    inscall(nm, sf_args[k]);
    e_c = 0; e_lv = 0; e_tmp = 0;
    if (stmt) ins0("drop");
    else if (r == 's') lret_to_temp(str_ty(255));
    else e_t = r == 'i' ? T_INT : r == 'b' ? T_BOOL : T_CHAR;
}

static void std_call(int id)
{
    if (id >= ST_SF) { simple_call((u8)(id - ST_SF), 0); return; }
    switch (id) {
    case ST_ABS:
    case ST_SQR:
        arg_open();
        expression();
        need_val();
        if (is_wide(e_t)) {
            inscall(ty_k[e_t] == TY_REAL ? (id == ST_ABS ? "__pfabs" : "__psqr") : (id == ST_ABS ? "__plabs" : "__plsqr"), 1);
            lret_to_temp(e_t);
        } else {
            if (host(e_t) != T_INT) err("an integer is expected");
            if (id == ST_ABS) {
                if (e_c) { if (e_v < 0) e_v = -e_v; } else { rvalue(); inscall("abs", 1); }
            } else if (e_c) e_v = e_v * e_v; else { rvalue(); ins0("dup"); ins0("mul"); }
            e_t = T_INT;
        }
        arg_close();
        break;
    case ST_ODD:
        arg_open(); expression(); narrow(); need_ord();
        if (e_c) e_v = e_v & 1; else { rvalue(); ins1("const", 1); ins0("and"); }
        e_t = T_BOOL; arg_close();
        break;
    case ST_ORD:
        arg_open(); expression(); narrow(); need_ord();
        if (!e_c) rvalue();
        e_t = T_INT; arg_close();
        break;
    case ST_CHR:
        arg_open(); expression(); narrow(); need_ord();
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
    case ST_FILEPOS:
    case ST_FILESIZE:
        if (tok == '(') {
            arg_open();
            expression();
            file_value();
            arg_close();
            inscall(id == ST_EOF ? "__pfeof" : id == ST_EOLN ? "__pfeoln" : id == ST_FILEPOS ? "__pfpos" : "__pfsize", 1);
        } else inscall(id == ST_EOF ? "__peof" : "__peoln", 0);
        e_c = 0; e_lv = 0; e_t = id == ST_FILEPOS || id == ST_FILESIZE ? T_INT : T_BOOL;
        break;
    case ST_IORESULT:
        e_c = 1; e_v = 0; e_t = T_INT;
        break;
    case ST_LENGTH:
        arg_open(); expression();
        if (!isstr(e_t)) err("a string is expected");
        ins0("load.ub");
        e_t = T_INT; e_lv = 0; e_tmp = 0; e_str = 0;
        arg_close();
        break;
    case ST_COPY:
        arg_open(); str_arg();
        expect(',', "a , is expected"); int_arg();
        expect(',', "a , is expected"); int_arg();
        {
            u16 t = new_temp(256);
            ins1("laddr", t);
            inscall("__pcopy", 4);
        }
        e_t = str_ty(255); e_lv = 1; e_tmp = 1; e_str = 0;
        arg_close();
        break;
    case ST_POS:
        arg_open(); str_arg();
        expect(',', "a , is expected"); str_arg();
        inscall("__ppos", 2);
        e_t = T_INT; e_lv = 0; e_tmp = 0; e_str = 0;
        arg_close();
        break;
    case ST_CONCAT:
        arg_open(); str_arg();
        while (tok == ',') {
            u8 lt = e_t, lm = e_tmp;
            next();
            expression();
            bin_ltmp = lm;
            str_binary('+', 0, 0, lt, 0);
        }
        arg_close();
        break;
    case ST_UPCASE:
        arg_open(); expression();
        if (host(e_t) != T_CHAR) err("a char is expected");
        rvalue();
        inscall("toupper", 1);
        ins0("zx");
        e_t = T_CHAR;
        arg_close();
        break;
    case ST_TRUNC: real_fn("__ptrunc", TYP_LONG); break;
    case ST_ROUND: real_fn("__pround", TYP_LONG); break;
    case ST_SQRT: real_fn("__psqrt", TYP_REAL); break;
    case ST_SIN: real_fn("__psin", TYP_REAL); break;
    case ST_COS: real_fn("__pcos", TYP_REAL); break;
    case ST_ARCTAN: real_fn("__patan", TYP_REAL); break;
    case ST_LN: real_fn("__pln", TYP_REAL); break;
    case ST_EXP: real_fn("__pexp", TYP_REAL); break;
    case ST_INT: real_fn("__pint", TYP_REAL); break;
    case ST_FRAC: real_fn("__pfrac", TYP_REAL); break;
    case ST_PI:
        wide_literal(0x4049, 0x0FDB);
        e_c = 0; e_t = TYP_REAL; e_lv = 1;
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
        if (!e_lv || ty_k[e_t] != TY_PTR) err("a pointer variable is expected");
        t = e_t;
        ins1("const", ty_size[ty_base[t]]);
        inscall("malloc", 1);
        ins0("store.w");
        ins0("drop");
        arg_close();
        break;
    }
    case ST_DISPOSE:
        arg_open();
        expression();
        if (ty_k[e_t] != TY_PTR) err("a pointer is expected");
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
    case ST_ASSIGN:
        arg_open();
        expression();
        if (!e_lv || ty_k[e_t] != TY_FILE) err("a file variable is expected");
        expect(',', "a , is expected");
        str_arg();
        inscall("__passign", 2);
        ins0("drop");
        arg_close();
        break;
    case ST_RESET:
    case ST_REWRITE:
    case ST_APPEND:
    case ST_CLOSE:
    case ST_SEEK: {
        u8 es;
        arg_open();
        expression();
        es = file_value();
        if (id == ST_SEEK) {
            expect(',', "a , is expected");
            int_arg();
            inscall("__pfseek", 2);
        } else if (id == ST_CLOSE) inscall("__pfclose", 1);
        else {
            ins1("const", es);
            inscall(id == ST_RESET ? "__preset" : id == ST_REWRITE ? "__prewrite" : "__pappend", 2);
        }
        ins0("drop");
        arg_close();
        break;
    }
    case ST_BREAK:
    case ST_CONTINUE:
        if (!nloop) err("not in a loop");
        insl("jmp", id == ST_BREAK ? brk_l[nloop - 1] : cont_l[nloop - 1]);
        break;
    case ST_EXIT:
        insl("jmp", cur_exit);
        break;
    case ST_INCLUDE:
    case ST_EXCLUDE:
        arg_open();
        expression();
        if (!e_lv || ty_k[e_t] != TY_SET) err("a set variable is expected");
        expect(',', "a , is expected");
        int_arg();
        ins0("swap");
        inscall(id == ST_INCLUDE ? "__pseta" : "__psetx", 2);
        ins0("drop");
        arg_close();
        break;
    case ST_DELETE:
        arg_open();
        expression();
        if (!e_lv || ty_k[e_t] != TY_STR) err("a string variable is expected");
        expect(',', "a , is expected"); int_arg();
        expect(',', "a , is expected"); int_arg();
        inscall("__pdelete", 3);
        ins0("drop");
        arg_close();
        break;
    case ST_INSERT: {
        u8 mx;
        arg_open();
        str_arg();
        expect(',', "a , is expected");
        expression();
        if (!e_lv || ty_k[e_t] != TY_STR) err("a string variable is expected");
        mx = (u8)tys[e_t].hi;
        expect(',', "a , is expected"); int_arg();
        ins1("const", mx);
        inscall("__pinsert", 4);
        ins0("drop");
        arg_close();
        break;
    }
    case ST_STR: {
        u8 k;
        arg_open();
        expression();
        k = ty_k[e_t];
        if (k != TY_REAL && k != TY_LONG) { narrow(); rvalue(); if (host(e_t) != T_INT) err("a number is expected"); }
        if (tok == ':') { next(); int_arg(); } else ins1("const", 0);
        if (k == TY_REAL) {
            if (tok == ':') { next(); int_arg(); } else ins1("const", -1);
        }
        expect(',', "a , is expected");
        expression();
        if (!e_lv || ty_k[e_t] != TY_STR) err("a string variable is expected");
        inscall(k == TY_REAL ? "__pstrr" : k == TY_LONG ? "__pstrg" : "__pstri", k == TY_REAL ? 4 : 3);
        ins0("drop");
        arg_close();
        break;
    }
    case ST_VAL: {
        u8 k;
        arg_open();
        str_arg();
        expect(',', "a , is expected");
        expression();
        k = ty_k[e_t];
        if (!e_lv || e_c || (k != TY_REAL && k != TY_LONG && host(e_t) != T_INT)) err("a number variable is expected");
        expect(',', "a , is expected");
        expression();
        if (!e_lv || host(e_t) != T_INT) err("an integer variable is expected");
        inscall(k == TY_REAL ? "__pvalr" : k == TY_LONG ? "__pvalg" : "__pvali", 3);
        ins0("drop");
        arg_close();
        break;
    }
    default:
        if (id >= ST_SF) { simple_call((u8)(id - ST_SF), 1); break; }
        err("this standard routine is not a statement");
    }
}

static void assignment(void)
{
    u8 t, w;
    expression();
    if (tok != T_ASSIGN && e_called) { ins0("drop"); return; }          /* p(args); */
    if (!e_lv || e_c) err("a variable is expected");
    t = e_t;
    w = e_word;
    if (tok != T_ASSIGN && ty_k[t] == TY_PROC) { call_var(); ins0("drop"); return; }   /* p; */
    expect(T_ASSIGN, "a := is expected");
    want_proc = ty_k[t] == TY_PROC ? t : 0;
    expression();
    if (ty_k[t] == TY_STR) {
        str_value();
        ins1("const", tys[t].hi);
        inscall("__pstrcpy", 3);
        ins0("drop");
        return;
    }
    if (e_str && ty_k[t] == TY_ARRAY && ty_k[ty_base[t]] == TY_CHAR) {
        ins1("const", 1);
        ins0("add");
        ins1("const", ty_size[t]);
        ins1("const", e_v);
        inscall("__pstr", 4);
        ins0("drop");
        return;
    }
    coerce(t);
    if (is_scalar(t)) {
        emit_store(t, w);
        ins0("drop");
    } else {
        if (!e_lv) err("a value of this type cannot be assigned");
        ins1("copy", ty_size[t]);
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
    if (ty_k[f->ty] == TY_STR) {
        str_value();
        ins1("const", tys[f->ty].hi);
        inscall("__pstrcpy", 3);
    } else {
        coerce(f->ty);
        if (is_scalar(f->ty)) emit_store(f->ty, 0); else ins1("copy", 4);
    }
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
    next();
    expression();
    need_ord();
    rvalue();
    t = new_temp(2);
    st_temp(t);
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
                ld_temp(t); ins1("const", lo); ins0("lt"); insl("jnz", lskip);
                ld_temp(t); ins1("const", hi); ins0("le"); insl("jnz", lbody);
                deflabel(lskip);
            } else {
                ld_temp(t); ins1("const", lo); ins0("eq"); insl("jnz", lbody);
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
}

static void with_statement(void)
{
    u8 n0 = nwith;
    next();
    for (;;) {
        u16 o;
        expression();
        if (!e_lv || ty_k[e_t] != TY_REC) err("a record variable is expected");
        if (nwith >= NWITH) err("with is nested too deep");
        o = new_temp(2);
        st_temp(o);
        with_ty[nwith] = e_t;
        with_off[nwith] = o;
        nwith++;
        if (tok == ',') { next(); continue; }
        break;
    }
    expect(K_DO, "do is expected");
    statement();
    nwith = n0;
}

static void for_statement(void)
{
    int vi;
    sym_t *s;
    u8 down, t, w;
    u16 tm, ltop, lbody, lend = newlab();
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
    st_temp(tm);
    expect(K_DO, "do is expected");
    ltop = newlab(); lbody = newlab();
    var_addr(s); emit_load(t, w); ld_temp(tm); ins0(down ? "lt" : "gt"); insl("jnz", lend);
    deflabel(lbody);
    loop_open(lend, ltop);
    statement();
    nloop--;
    deflabel(ltop);
    var_addr(s); emit_load(t, w); ld_temp(tm); ins0("eq"); insl("jnz", lend);
    var_addr(s); ins0("dup"); emit_load(t, w); ins1("const", 1); ins0(down ? "sub" : "add"); emit_store(t, w); ins0("drop");
    insl("jmp", lbody);
    deflabel(lend);
}

static void statement(void)
{
    u16 save = fpv[cur_level];
    nest += 4;
    if (nest > 78) err("the statements are nested too deep");
    if (tok == T_NUM) {            /* a label */
        deflabel((u16)(20000 + tokv));
        next();
        expect(':', "a : is expected");
    }
    switch (tok) {
    case K_GOTO:
        next();
        if (tok != T_NUM) err("a label is expected");
        insl("jmp", (u16)(20000 + tokv));
        next();
        break;
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
        loop_open(lend, ltop);
        statement();
        nloop--;
        insl("jmp", ltop);
        deflabel(lend);
        break;
    }
    case K_REPEAT: {
        u16 ltop = newlab(), lcont = newlab(), lend = newlab();
        next();
        deflabel(ltop);
        loop_open(lend, lcont);
        stat_list(K_UNTIL, K_UNTIL);
        nloop--;
        expect(K_UNTIL, "until is expected");
        deflabel(lcont);
        cond_jump("jz", ltop);
        deflabel(lend);
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
    nest -= 4;
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
                put(".bss "); put(nm); put(" "); putn(ty_size[t]); put("\n");
            } else {
                s->off = fpv[cur_level];
                fpv[cur_level] = (u16)(fpv[cur_level] + ty_size[t]);
                if (fpv[cur_level] > maxfpv[cur_level]) maxfpv[cur_level] = fpv[cur_level];
            }
        }
        expect(';', "a ; is expected");
    }
}

static u16 nstatic;

/* the value of a typed constant (an initialized variable), as data lines */
static void init_data(u8 t)
{
    u8 ct;
    int v, n, i;
    if (is_ord(t)) {
        v = const_value(&ct);
        w_puts(&sf, ty_size[t] == 1 ? ".b " : ".w ");
        w_num(&sf, v);
        w_putc(&sf, '\n');
    } else if (ty_k[t] == TY_STR) {
        if (tok != T_STR || toklen > (u16)tys[t].hi) err("a string of the right length is expected");
        w_puts(&sf, ".b ");
        w_num(&sf, (int)toklen);
        w_putc(&sf, '\n');
        if (toklen) put_s(toks, toklen);
        if (tys[t].hi > (int)toklen) { w_puts(&sf, ".z "); w_num(&sf, tys[t].hi - (int)toklen); w_putc(&sf, '\n'); }
        next();
    } else if (ty_k[t] == TY_ARRAY) {
        n = tys[t].hi - tys[t].lo + 1;
        expect('(', "a ( is expected");
        for (i = 0; i < n; i++) {
            init_data(ty_base[t]);
            if (i < n - 1) expect(',', "a , is expected");
        }
        expect(')', "a ) is expected");
    } else err("this type cannot have a value here");
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
        if (tok == ':') {                      /* a typed constant */
            char tn[8];
            next();
            t = parse_type();
            expect('=', "a = is expected");
            s = new_sym(nm, SK_VAR);
            s->ty = t;
            s->flags = F_STATIC;
            s->val = (int)++nstatic;
            pname(tn, 'T', nstatic);
            w_puts(&sf, ".data ");
            w_puts(&sf, tn);
            w_puts(&sf, " local\n");
            init_data(t);
            w_puts(&sf, ".end\n");
            expect(';', "a ; is expected");
            continue;
        }
        expect('=', "a = is expected");
        if (tok == T_STR && toklen > 1) {
            if (strlen_pool + toklen + 1 > sizeof(strpool)) err("too many string constants");
            memcpy(strpool + strlen_pool, toks, toklen);
            s = new_sym(nm, SK_CONST);
            s->ty = str_ty(toklen);
            s->val = (int)strlen_pool;
            strlen_pool += toklen;
            next();
        } else {
            u8 neg = 0;
            if (tok == '-' || tok == '+') { neg = tok == '-'; next(); }
            if (tok == T_REAL) {
                if (strlen_pool + toklen + 2 > sizeof(strpool)) err("too many string constants");
                s = new_sym(nm, SK_CONST);
                s->ty = TYP_REAL;
                s->val = (int)strlen_pool;
                if (neg) strpool[strlen_pool++] = '-';
                memcpy(strpool + strlen_pool, toks, toklen);
                strlen_pool += toklen;
                s->off = (u16)(strlen_pool - (u16)s->val);
                next();
            } else if (tok == T_NUM && tokh) {
                u16 lo = (u16)tokv, hi = tokh;
                if (neg) { lo = (u16)(-lo); hi = (u16)(~hi + (lo == 0)); }
                s = new_sym(nm, SK_CONST);
                s->ty = TYP_LONG;
                s->val = (int)hi;
                s->off = lo;
                next();
            } else {
                v = const_value(&t);
                if (neg) v = -v;
                s = new_sym(nm, SK_CONST);
                s->ty = t;
                s->val = v;
            }
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
        ty_base[fwdptrs[i].t] = syms[k].ty;
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
    u8 bt[16], br[16], n = 0, k;           /* (kept here until the list is complete: a procedural parameter has a list of its own) */
    s->pn = npn;
    if (tok == '(') {
        next();
        for (;;) {
            u8 byref = 0, first = n, i, t;
            if (tok == K_PROCEDURE || tok == K_FUNCTION) {          /* a procedural parameter: function f(x: integer): integer */
                u8 isf = tok == K_FUNCTION;
                next();
                if (tok != T_ID) err("a name is expected");
                if (n >= 16 || npn >= NPN) err("too many parameters");
                strcpy(par_name[npn++], toks);
                br[n] = 0;
                next();
                bt[n++] = proc_type(isf);
                if (tok == ';') { next(); continue; }
                break;
            }
            if (tok == K_VAR) { byref = 1; next(); }
            for (;;) {
                if (tok != T_ID) err("a name is expected");
                if (n >= 16 || npn >= NPN) err("too many parameters");
                strcpy(par_name[npn++], toks);
                br[n++] = byref;
                next();
                if (tok == ',') { next(); continue; }
                break;
            }
            expect(':', "a : is expected");
            if (tok != T_ID) err("a type name is expected");
            t = find_type(toks);
            next();
            for (i = first; i < n; i++) bt[i] = t;
            if (tok == ';') { next(); continue; }
            break;
        }
        expect(')', "a ) is expected");
    }
    if (npar + n > NPAR) err("too many parameters");
    s->pf = (u8)npar;
    s->np = n;
    for (k = 0; k < n; k++) { par_ty[npar] = bt[k]; par_ref[npar++] = br[k]; }
}

static void enter_params(sym_t *f)
{
    u8 j;
    for (j = 0; j < f->np; j++) {
        u8 pt = par_ty[f->pf + j];
        u8 slot = (u8)(f->np - 1 - j);
        sym_t *s;
        check_new(par_name[f->pn + j]);
        s = new_sym(par_name[f->pn + j], SK_VAR);
        s->ty = pt;
        s->val = slot;
        if (par_ref[f->pf + j]) { s->flags = F_PARAM | F_BYREF; s->off = slot; }
        else if (is_scalar(pt)) { s->flags = F_PARAM; s->off = slot; }
        else {
            s->flags = F_COPYIN;
            s->off = fpv[cur_level];
            fpv[cur_level] = (u16)(fpv[cur_level] + ty_size[pt]);
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
            if (!is_scalar(s->ty) && !is_wide(s->ty) && ty_k[s->ty] != TY_STR) err("a function returns a simple value");
            next();
        }
    } else {
        s = &syms[si];
        if (tok == '(') {                       /* the parameters again (Turbo Pascal): the first list stays */
            u8 pf = s->pf, pn = s->pn, np = s->np, k = npn;
            u16 kp = npar;
            param_list(s);
            s->pf = pf; s->pn = pn; s->np = np;
            npar = kp; npn = k;
        }
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
    if (npn == s->pn + s->np) npn = s->pn;
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
        res = new_temp(ty_size[syms[fs].ty]);
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
    if (fs < 0 && heap_size) { put(".bss __pheapm "); putn(heap_size); put("\n"); }
    put(".func ");
    if (fs < 0) put("main"); else { pname(nm, 'P', (u16)syms[fs].val); put(nm); }
    put(" ");
    frame_patch = w_tell(&ir);
    frame_ph = w_tellh(&ir);
    put("00000\n");
    if (fs < 0 && heap_size) { insaddr("__pheapm"); ins1("const", heap_size); inscall("__psetheap", 2); ins0("drop"); }
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
            ins1("copy", ty_size[s->ty]);
            ins0("drop");
        } else if (s->flags & F_COPIED) {
            ins1("laddr", s->off);
            ins1("paddr", s->val);
            ins0("load.w");
            ins0("store.w");
            ins0("drop");
        }
    }
    cur_exit = newlab();
    nloop = 0;
    if (tok != K_END) stat_list(K_END, K_END);
    expect(K_END, "end is expected");
    deflabel(cur_exit);
    if (isfunc && !is_scalar(syms[fs].ty)) {
        insaddr(ty_size[syms[fs].ty] == 4 ? "__lret" : "__sret");
        ins1("laddr", res);
        ins1("copy", ty_size[syms[fs].ty]);
        ins0("drop");
        ins1("const", 0);
        ins0("ret");
        lh_used[LH_RET] = 1;
    } else if (isfunc) {
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
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < tail) dst = argv[++i];
        else if (!strcmp(argv[i], "-v")) return cc_version("pas1");
        else if (argv[i][0] == '-') { puts("pas1: unknown option"); return 2; }
        else src = argv[i];
    }
    if (!src) { puts("usage: pas1 -o out.ir file.pas"); return 2; }
    if (!dst) { puts("pas1: -o is needed"); return 2; }
    if (r_open(&in, src)) { puts("pas1: cannot open the input"); return 2; }
    if (w_open(&ir, dst)) { puts("pas1: cannot create the output"); return 2; }
    if (w_open(&sf, "pas1s.tmp")) { puts("pas1: cannot create pas1s.tmp"); return 2; }
    ty_k[T_VOID] = TY_VOID;
    ty_k[T_INT] = TY_INT; ty_size[T_INT] = 2; tys[T_INT].lo = -32768; tys[T_INT].hi = 32767;
    ty_k[T_CHAR] = TY_CHAR; ty_size[T_CHAR] = 1; tys[T_CHAR].lo = 0; tys[T_CHAR].hi = 255;
    ty_k[T_BOOL] = TY_BOOL; ty_size[T_BOOL] = 1; tys[T_BOOL].lo = 0; tys[T_BOOL].hi = 1;
    ty_k[T_NILT] = TY_NIL; ty_size[T_NILT] = 2;
    ty_k[T_BYTE] = TY_SUB; ty_size[T_BYTE] = 1; ty_base[T_BYTE] = T_INT; tys[T_BYTE].lo = 0; tys[T_BYTE].hi = 255;
    ty_k[TYP_REAL] = TY_REAL; ty_size[TYP_REAL] = 4;
    ty_k[TYP_LONG] = TY_LONG; ty_size[TYP_LONG] = 4;
    ty_k[TYP_TEXT] = TY_FILE; ty_size[TYP_TEXT] = 2;
    ty_k[TYP_WORD] = TY_SUB; ty_size[TYP_WORD] = 2; ty_base[TYP_WORD] = T_INT; tys[TYP_WORD].hi = -1;
    ty_k[TYP_SHORT] = TY_SUB; ty_size[TYP_SHORT] = 1; tys[TYP_SHORT].sg = 1; ty_base[TYP_SHORT] = T_INT; tys[TYP_SHORT].lo = -128; tys[TYP_SHORT].hi = 127;
    nty = 11;
    scope_mark = nsym;
    line = 1;
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
    put(".stack "); putn(stack_size); put("\n");
    if (tok != '.') err("a . is expected at the end of the program");
    {
        u8 k;
        for (k = 0; k < LH_N; k++) if (lh_used[k]) { put(".extern "); put(lh_name[k]); put("\n"); }
    }
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
