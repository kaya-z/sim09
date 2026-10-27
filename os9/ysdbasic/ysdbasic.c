/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * ysdbasic - YSD BASIC: a small BASIC interpreter for OS-9 (NitrOS-9 Level 1 / Level 2), in the manner of the Microsoft BASICs of the 1980s
 *
 * The port of the BASIC of MK09 (mk09/apps/basic/basic.c) to OS-9: the interpreter, the internal form and the compiled expressions are
 * those of MK09 BASIC; what is different is the system interface (OS-9 calls instead of the MK09 library: sc_*), the end of a line (CR) and
 * the clock.  Written in C for CMOC (--os9).
 *
 *   ysdbasic [program.bas]    without a file: the interactive mode; with a file: load it, run it, leave
 *
 * Numbers are 32 bit floating point (sfp.h, software), strings are up to 63 bytes.
 *   Commands : NEW LIST [a[-b]] RUN [n] LOAD "f" SAVE "f" BYE (QUIT, SYSTEM)
 *   Statements: REM ' LET PRINT ? INPUT IF..THEN..ELSE GOTO GOSUB RETURN FOR..TO..STEP NEXT END STOP
 *             DIM CLS ON..GOTO/GOSUB DATA READ RESTORE RANDOMIZE
 *   Operators : + - * / ^ = <> < > <= >= AND OR NOT, strings: + and comparison
 *   Functions : ABS INT SGN SQR SIN COS TAN ATN EXP LOG RND LEN ASC VAL CHR$ STR$ LEFT$ RIGHT$ MID$ SPC TAB TIMER
 * Variables: a letter and one more letter or digit are significant (A, AB, A1), A$ for strings
 * (one letter only), arrays of numbers with one or two dimensions (DIM A(10), B(3,4); 11 elements by default).
 * The program is kept in an internal form: when a line is entered, the keywords, the functions, the names of the variables and the numbers are
 * resolved once (tokenize()); LIST and SAVE give the text back (detok()). The expressions are compiled, when they are first run, to a postfix code
 * for a small stack machine (c_expr(), run()) that is kept until the program changes; a LET of a number variable is compiled as a whole.
 * ^C leaves the program (OS-9 sends the interrupt signal to the process).  Files are OS-9 text files (lines end with CR).
 */
#include <cmoc.h>
#include "types.h"
#include "sfpm.h"

#define BASIC_VERSION "1.0"     /* ysdbasic 1.0: the BASIC of MK09 (its version 1.1) on OS-9 */
#ifndef PGM
#define PGM     3600           /* the program in its internal form: what fits next to the module in a Level 1 system (make ysdbasic-l2: more) */
#endif
#ifndef NVARS
#define NVARS   80             /* number variables: a name gets a slot when it is first met (varslot()); 962 names are possible, 26 * 37 */
#endif
#ifndef APOOL
#define APOOL   300
#endif
#define STRL    40
#define LINEMAX 200
#define FORMAX  8
#define GOSMAX  16
#define OBUF    128


/* ---------------------------------------------------------------- OS-9 calls (as in os9/jwp/jwp.c) */

static int os9err;

static int sc_read(int path, u8 *b, int n)
{
    int r;
    asm {
        pshs y
        ldd  :path
        tfr  b,a
        ldx  :b
        ldy  :n
        os9  $89
        bcs  rderr
        sty  :r
        puls y
        bra  rddone
rderr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
rddone  equ  *
    }
    return r;
}

/* a line from a path: up to n bytes, to and with the CR (I$ReadLn) */
static int sc_readln(int path, u8 *b, int n)
{
    int r;
    asm {
        pshs y
        ldd  :path
        tfr  b,a
        ldx  :b
        ldy  :n
        os9  $8B
        bcs  rlerr
        sty  :r
        puls y
        bra  rldone
rlerr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
rldone  equ  *
    }
    return r;
}

static int sc_write(int path, const u8 *b, int n)
{
    int r;
    asm {
        pshs y
        ldd  :path
        tfr  b,a
        ldx  :b
        ldy  :n
        os9  $8A
        bcs  wrerr
        sty  :r
        puls y
        bra  wrdone
wrerr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
wrdone  equ  *
    }
    return r;
}

static int sc_open(const char *name, int mode)
{
    int r;
    asm {
        pshs y
        ldd  :mode
        tfr  b,a
        ldx  :name
        os9  $84
        bcs  operr
        tfr  a,b
        clra
        std  :r
        puls y
        bra  opdone
operr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
opdone  equ  *
    }
    return r;
}

static int sc_create(const char *name)
{
    int r;
    asm {
        pshs y
        lda  #$02
        ldb  #$0B
        ldx  :name
        os9  $83
        bcs  crerr
        tfr  a,b
        clra
        std  :r
        puls y
        bra  crdone
crerr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
crdone  equ  *
    }
    return r;
}

/* up to and with the first CR, or n bytes (I$WritLn: the driver adds the line feed) */
static int sc_writeln(int path, const u8 *b, int n)
{
    int r;
    asm {
        pshs y
        ldd  :path
        tfr  b,a
        ldx  :b
        ldy  :n
        os9  $8C
        bcs  wlerr
        sty  :r
        puls y
        bra  wldone
wlerr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
wldone  equ  *
    }
    return r;
}

static void sc_close(int path)
{
    asm {
        pshs y
        ldd  :path
        tfr  b,a
        os9  $8F
        puls y
    }
}

static int sc_delete(const char *name)
{
    int r;
    asm {
        pshs y
        ldx  :name
        os9  $87
        bcs  dlerr
        ldd  #0
        std  :r
        puls y
        bra  dldone
dlerr   puls y
        clra
        std  :os9err
        ldd  #-1
        std  :r
dldone  equ  *
    }
    return r;
}

/* F$Time: the date and the time (year, month, day, hour, minute, second) */
static void sc_time(u8 *t6)
{
    asm {
        pshs y
        ldx  :t6
        os9  $15
        puls y
    }
}

/* the seconds since midnight (TIMER, RANDOMIZE) */
static u32 secs(void)
{
    u8 t[6];
    sc_time(t);
    return (u32)t[3] * 3600UL + (u32)t[4] * 60UL + t[5];
}

static u16 utoa_(u16 v, char *s, u8 base)
{
    char t[8];
    u8 n = 0, i;
    u16 len;
    do { u8 d = (u8)(v % base); t[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10); v /= base; } while (v);
    for (i = 0; i < n; i++) s[i] = t[n - 1 - i];
    s[n] = 0;
    len = n;
    return len;
}

/* a line of the console without its CR; -1 at the end of the input */
static int gets_(char *buf, u16 n)
{
    int r = sc_readln(0, (u8 *)buf, (int)n - 1);
    if (r < 0) return -1;
    if (r && buf[r - 1] == '\r') r--;
    buf[r] = 0;
    return r;
}

/* ---- output */
static u8 ob[OBUF];
static u8 on, col;

static void flush(void)
{
    u8 *p = ob;
    while (on) {                                    /* a line at a time (I$WritLn), the rest as it is */
        u8 n = 0;
        while (n < on && p[n] != '\r') n++;
        if (n < on) { n++; sc_writeln(1, p, n); }
        else sc_write(1, p, n);
        p += n;
        on -= n;
    }
    on = 0;
}

static void outc(char c)
{
    if (on == OBUF) flush();
    ob[on++] = (u8)(c == '\n' ? '\r' : c);          /* OS-9: a line ends with CR */
    if (c == '\n') col = 0; else col++;
}

static void outs(const char *s)
{
    while (*s) outc(*s++);
}

static void outn(u16 v)
{
    char b[8];
    utoa_(v, b, 10);
    outs(b);
}

/* ---- program and variables */
static f32 poolw[PGM / 4];           /* lines: [u16 number][u8 len][the internal form of the text] (words: the numbers inside are aligned) */
#define pool ((u8 *)poolw)
static u8 *pend;                     /* end of the program */
static f32 numv[NVARS];
static u16 vname[NVARS];             /* the name (c1 * 37 + c2) of each slot */
static u8 nvused;                    /* slots in use */

/* the slot of a variable name; the slots are given out in the order the names are met (the program keeps the byte offset), and are all free
 * again when the program is thrown away (NEW, LOAD) */
static int varslot(u16 code)
{
    u8 i;
    for (i = 0; i < nvused; i++)
        if (vname[i] == code) return i;
    if (nvused >= NVARS) return -1;
    vname[nvused] = code;
    return nvused++;
}
static char strv[26][STRL];
static f32 apool[APOOL];
static u16 aused;
typedef struct { u16 off; u8 d1, d2; u8 dim; } adesc;
static adesc arr[26];

static f32 dbufw[66];                /* the line of the direct mode: same layout as a program line */
#define dbuf ((char *)dbufw)
static u8 *cl;                       /* current line */
static char *tp;                     /* current position in the line */
static u8 running, bye, loaded;
static const char *errmsg;

typedef struct { u16 vi; f32 lim, step; u8 *line; char *tp; } forent;
static forent fors[FORMAX];
static u8 nfor;
typedef struct { u8 *line; char *tp; } gosent;
static gosent gos[GOSMAX];
static u8 ngos;
static u8 *dline;                    /* DATA pointer */
static char *dtp;
static u32 rseed = 0x2545F491UL;

#define LNUM(p)  (((u16)(p)[0] << 8) | (p)[1])
#define LLEN(p)  ((p)[2])
#define LTEXT(p) ((char *)(p) + 3)
#define NEXTLINE(p) ((p) + 3 + LLEN(p))
#define TK(p)    ((u8)*(p))                  /* the byte at p as a token */
#define OFS(p)   (((u16)(u8)(p)[1] << 8) | (u8)(p)[2])      /* the two bytes after a token */

/* ---- the internal form of a program line: [u16 number][u8 len][body]. (The YSD9000 needs the numbers at even addresses and keeps the lines of an even size; the 6809 does not care.)
 * In the body the keywords and the functions are one byte (>= 0x80) and the names and the numbers are resolved once, when the line is entered:
 *   T_NV  hi lo       a number variable: hi lo is the byte offset in numv[] (the names are of 2 significant characters)
 *   T_SV  letter      a string variable (A$);   T_AV letter   an array (the '(' follows as a character)
 *   T_XN  n chars     follows T_NV/T_SV/T_AV when the name is longer (the rest of the characters: for LIST only)
 *   T_NUM f32 n chars   a number: the value, then the text as it was typed (for LIST)
 *   T_LN  hi lo       a line number after GOTO GOSUB THEN ELSE ON..GOTO, RUN and LIST
 *   T_QM, T_QREM      '?' and '\'' (the same as PRINT and REM, listed as typed)
 * Strings in quotes, and the text after REM and DATA, are kept as typed. */
enum { T_NV = 0x80, T_SV, T_AV, T_XN, T_NUM, T_LN, T_QM, T_QREM,
       T_FN0 = 0x88,                                /* the 20 functions: 0x88 - 0x9B */
       T_REM = 0xA0, T_PRINT, T_LET, T_IF, T_THEN, T_ELSE, T_GOTO, T_GOSUB, T_RETURN, T_FOR, T_TO, T_STEP, T_NEXT, T_END, T_STOP, T_DIM, T_ON,
       T_DATA, T_READ, T_RESTORE, T_RANDOMIZE, T_INPUT, T_CLS, T_RUN, T_LIST, T_LOAD, T_SAVE, T_NEW, T_BYE, T_QUIT, T_SYSTEM,
       T_AND, T_OR, T_NOT, T_TAB, T_SPC };
static const char kwnames[] = "REM\0PRINT\0LET\0IF\0THEN\0ELSE\0GOTO\0GOSUB\0RETURN\0FOR\0TO\0STEP\0NEXT\0END\0STOP\0DIM\0ON\0DATA\0READ\0RESTORE\0RANDOMIZE\0INPUT\0CLS\0RUN\0LIST\0LOAD\0SAVE\0NEW\0BYE\0QUIT\0SYSTEM\0AND\0OR\0NOT\0TAB(\0SPC(\0";
#define NKW 36

static void fail(const char *m) { if (!errmsg) errmsg = m; }

/* a file error with the OS-9 error number: "File not found (#216)" */
static char errbuf[40];
static void failos(const char *m)
{
    char t[8];
    strcpy(errbuf, m);
    strcat(errbuf, " (#");
    utoa_((u16)(os9err & 255), t, 10);
    strcat(errbuf, t);
    strcat(errbuf, ")");
    fail(errbuf);
}

static void clear_vars(void)
{
    u16 i;
    for (i = 0; i < NVARS; i++) numv[i] = 0;
    memset(strv, 0, sizeof(strv));
    memset(arr, 0, sizeof(arr));
    aused = 0;
    nfor = ngos = 0;
    dline = 0;
    dtp = 0;
}

/* ---- text scanning */
#define skipsp() do { while (*tp == ' ') tp++; } while (0)       /* (macros: this is the hottest code of the interpreter) */

static int eat(u8 t)                       /* consume the token t if it is next */
{
    skipsp();
    if (TK(tp) == t) { tp++; return 1; }
    return 0;
}

#define ext() do { if (TK(tp) == T_XN) tp += 2 + (u8)tp[1]; } while (0)         /* skip the rest of a long name */

#define isalpha_(c) ((u8)((c) - 'A') <= 'Z' - 'A')
#define isalnum_(c) (isalpha_(c) || (u8)((c) - '0') <= 9)

/* ---- values: the result of an expression is left in vt (0 number, 1 string), vf and vs
 * (CMOC passes structures by value in a very expensive way, so no structure is returned) */
static u8 vt;
static f32 vf;
static char *vs;

static char tmp[4][STRL];
static u8 tmpi;

static char *newtmp(void)
{
    tmpi = (u8)((tmpi + 1) & 3);
    return tmp[tmpi];
}

#define setn(f) do { vf = (f); vt = 0; vs = 0; } while (0)           /* (a macro: the argument is evaluated once) */

static void sets(char *s) { vt = 1; vf = 0; vs = s; }

static void expr(void);

static f32 vnum_bad(void) { fail("Type mismatch"); return 0; }
#define vnum() (vt ? vnum_bad() : vf)                  /* the result as a number */

static f32 getnum(void)
{
    expr();
    return vnum();
}

static s32 getint(void)
{
    return f_to_long(getnum());
}

static void dimarr(u8 a, u16 d1, u16 d2, u8 dim)
{
    u32 n = (u32)(d1 + 1) * (u32)(d2 + 1);
    u16 i;
    if (arr[a].dim) { fail("Redimensioned array"); return; }
    if (d1 > 255 || d2 > 255 || aused + n > APOOL) { fail("Out of memory"); return; }
    arr[a].off = aused;
    arr[a].d1 = (u8)d1;
    arr[a].d2 = (u8)d2;
    arr[a].dim = dim;
    for (i = 0; i < (u16)n; i++) apool[aused + i] = 0;
    aused += (u16)n;
}

/* address of the element a(i,j) of the array a */
static f32 *elem_ij(u8 a, s32 i, s32 j)
{
    u16 ix;
    if (!arr[a].dim) dimarr(a, 10, 0, 1);
    if (errmsg) return &apool[0];
    if (i < 0 || i > arr[a].d1 || j < 0 || j > arr[a].d2 || (arr[a].dim == 1 && j)) { fail("Subscript out of range"); return &apool[0]; }
    ix = (u16)((u16)i * (u16)(arr[a].d2 + 1) + (u16)j);
    return &apool[arr[a].off + ix];
}

/* the element a(i[,j]) in the text; the '(' is consumed by the caller */
static f32 *elem(u8 a)
{
    s32 i = getint(), j = 0;
    if (*tp == ',') { tp++; j = getint(); }
    if (*tp == ')') tp++; else fail("Syntax error");
    if (errmsg) return &apool[0];
    return elem_ij(a, i, j);
}

/* a variable that is assigned: leaves the address in lv_f (a number) or lv_s (a string); 0 if there is no variable at tp */
static f32 *lv_f;
static char *lv_s;
static u8 lval(void)
{
    u8 t = TK(tp), a;
    lv_s = 0;
    if (t == T_NV) { lv_f = (f32 *)((u8 *)numv + OFS(tp)); tp += 3; ext(); return 1; }
    if (t == T_SV) {
        lv_s = strv[(u8)tp[1]];
        tp += 2;
        if (TK(tp) == T_XN) { fail("Syntax error"); ext(); }       /* (string names: one letter) */
        return 1;
    }
    if (t == T_AV) {
        a = (u8)tp[1];
        tp += 2;
        ext();
        skipsp();
        if (*tp != '(') { fail("Syntax error"); lv_f = &apool[0]; return 1; }
        tp++;
        lv_f = elem(a);
        return 1;
    }
    return 0;
}

/* ---- functions */
static const char fnames[] = "ABS\0INT\0SGN\0SQR\0SIN\0COS\0TAN\0ATN\0EXP\0LOG\0RND\0LEN\0ASC\0VAL\0CHR$\0STR$\0LEFT$\0RIGHT$\0MID$\0TIMER\0";
#define NFN 20
static void numstr(f32 f, char *b)             /* like the BASICs: a space for the sign */
{
    char t[16];
    u8 n = 0;
    if (!(f & F_SIGN)) b[n++] = ' ';
    f_format(f, t);
    strcpy(b + n, t);
}

static f32 rnd01(void)
{
    rseed = rseed * 1664525UL + 1013904223UL;
    return f_mul(f_from_long((s32)((rseed >> 8) & 0xFFFFFFUL)), 0x33800000UL);   /* / 2^24 */
}

/* ---- functions: the arguments are on the stack of the expression machine below (cells of 4 bytes: a number, or a string pointer in the first
 * word); the types are known when the expression is compiled. The result is left in the place of the first argument. */
#define MAXD 10
static f32 vv_f[MAXD];
#define CSTR(c) (*(char **)(c))             /* the string in a cell */

static void fn_apply(u8 id, u8 n, f32 *b)
{
    char *as = CSTR(b), *s, *rs = 0;
    f32 f = *b;
    s32 nn, m, p2 = 0;
    if (id == 19) f = f_from_long((s32)secs());      /* TIMER: seconds since midnight */
    else if (id == 10) f = rnd01();                                                  /* RND */
    else if (id >= 16) {                            /* LEFT$ RIGHT$ MID$ */
        nn = f_to_long(b[1]);
        if (n == 3) p2 = f_to_long(b[2]);
        s = newtmp();
        s[0] = 0;
        m = (s32)strlen(as);
        if (id == 16) {
            if (nn > m) nn = m;
            if (nn < 0) nn = 0;
            memcpy(s, as, (u16)nn);
            s[(u16)nn] = 0;
        } else if (id == 17) {
            if (nn > m) nn = m;
            if (nn < 0) nn = 0;
            strcpy(s, as + (u16)(m - nn));
        } else {                                     /* MID$(s, start [, len]) */
            if (nn < 1) nn = 1;
            if (nn <= m) {
                if (n != 3 || p2 > m - (nn - 1)) p2 = m - (nn - 1);
                if (p2 < 0) p2 = 0;
                memcpy(s, as + (u16)(nn - 1), (u16)p2);
                s[(u16)p2] = 0;
            }
        }
        rs = s;
    } else if (id == 15) {                          /* STR$ */
        rs = newtmp();
        numstr(f, rs);
    } else if (id == 14) {                          /* CHR$ */
        rs = newtmp();
        rs[0] = (char)f_to_long(f);
        rs[1] = 0;
    } else if (id == 11) f = f_from_long((s32)strlen(as));
    else if (id == 12) f = f_from_long((s32)(u8)as[0]);
    else if (id == 13) {                            /* VAL */
        u8 neg = 0;
        while (*as == ' ') as++;
        if (*as == '-') { neg = 1; as++; } else if (*as == '+') as++;
        f_parse(as, &f);
        if (neg) f = f_neg(f);
    } else switch (id) {
    case 0: f = f_abs(f); break;
    case 1: f = f_int(f); break;
    case 2:
        nn = f_cmp(f, 0);
        if (nn > 0) f = F_ONE; else if (nn < 0) f = F_SIGN | F_ONE; else f = 0;
        break;
#ifndef NOMATH
    case 3: if (f & F_SIGN) fail("Illegal function call"); f = f_sqr(f); break;
    case 4: f = f_sin(f); break;
    case 5: f = f_cos(f); break;
    case 6: { f32 c2 = f_cos(f); if (!c2) { sfp_err = 2; f = F_MAX; } else f = f_div(f_sin(f), c2); } break;
    case 7: f = f_atn(f); break;
    case 8: f = f_exp(f); break;
    case 9: if (f_cmp(f, 0) <= 0) fail("Illegal function call"); else f = f_log(f); break;
#else
    case 3: case 4: case 5: case 6: case 7: case 8: case 9: fail("Illegal function call"); break;
#endif
    }
    if (rs) CSTR(b) = rs; else *b = f;
}

static void checkerr(void)
{
    if (sfp_err) {
        fail(sfp_err == 2 ? "Division by zero" : sfp_err == 3 ? "Illegal function call" : "Overflow");
        sfp_err = 0;
    }
}

/* ---- expressions: compiled once to a postfix code for a small stack machine; the code is kept for the place (the address) of the expression in the
 * program text and used again until the program changes. The types (number, string) are known at the compilation, so that the machine does not
 * test them (a wrong type is a C_TYPERR in the code). The result is left in vt, vf and vs. */
enum { C_END, C_NEG, C_CMP, C_SCMP, C_STR, C_SV, C_AV, C_FN, C_POW, C_NOT, C_AND, C_OR, C_CAT, C_TYPERR,
       C_DIV, C_SUB, C_ADD, C_MUL, C_STV,
       C_MULV, C_MULN, C_DIVV, C_DIVN, C_ADDV, C_ADDN, C_SUBV, C_SUBN, C_NUM, C_NV };    /* (the fused operations: the right operand is a variable (V) or a number (N)) */
#define NARENA 450
#define NCE    32
typedef struct { char *p; char *end; u16 *code; u16 gen; u8 kind; } cent_t;    /* kind: 0 an expression of a number, 1 of a string, 2 a LET statement */
static cent_t cache[NCE];
static u16 arena[NARENA];
static u16 *cfree, *cw;                      /* the free place in the arena, the place the code is written to */
static u16 cgen = 1;
static u8 cdep, cfail, cfull;
#define INPOOL(p) ((u16)((p) - (char *)pool) < PGM)         /* (only the program text is kept; the line of the direct mode is at the same address every time) */

static void cflush(void) { cgen++; cfree = arena; }         /* (the program changed) */

#define EM(w) do { if (cw >= arena + NARENA) cfull = 1; else *cw++ = (u16)(w); } while (0)

static void c_push(void) { if (++cdep > MAXD) cfail = 1; }
static u8 c_expr(void);

/* a binary operation (0 MUL, 1 DIV, 2 ADD, 3 SUB): the right operand was compiled from mark on */
static void c_bin(u8 k, u16 *mark)
{
    cdep--;
    if (cw - mark == 2 && (*mark == C_NV || *mark == C_NUM)) {
        *mark = (u16)(C_MULV + k * 2 + (*mark == C_NUM));
        return;
    }
    EM(k == 0 ? C_MUL : k == 1 ? C_DIV : k == 2 ? C_ADD : C_SUB);
}

static u8 c_primary(void)
{
    u8 t, n, id, ty;
    skipsp();
    t = TK(tp);
    if (t == T_NV) { EM(C_NV); EM(OFS(tp)); tp += 3; ext(); c_push(); return 0; }
    if (t == T_NUM) {
        f32 *q = (f32 *)(tp + 1);
        EM(C_NUM); EM((u16)q);
        tp = (char *)q + 5 + ((u8 *)q)[4];
        c_push();
        return 0;
    }
    if (t == '(') {
        tp++;
        ty = c_expr();
        skipsp();
        if (*tp == ')') tp++; else cfail = 1;
        return ty;
    }
    if (t == '"') {
        EM(C_STR); EM((u16)tp);
        tp++;
        while (*tp && *tp != '"') tp++;
        if (*tp) tp++;
        c_push();
        return 1;
    }
    if (t == T_SV) {
        if (TK(tp + 2) == T_XN) cfail = 1;                          /* (string names: one letter) */
        EM(C_SV); EM(tp[1]);
        tp += 2;
        c_push();
        return 1;
    }
    if (t == T_AV) {
        u8 a = (u8)tp[1];
        tp += 2;
        ext();
        skipsp();
        if (*tp != '(') { cfail = 1; return 0; }
        tp++;
        if (c_expr()) EM(C_TYPERR);
        n = 1;
        skipsp();
        if (*tp == ',') { tp++; if (c_expr()) EM(C_TYPERR); n = 2; skipsp(); }
        if (*tp == ')') tp++; else cfail = 1;
        EM(C_AV); EM(a | ((u16)n << 8));
        cdep -= n - 1;
        return 0;
    }
    if (t >= T_FN0 && t < T_FN0 + NFN) {
        u8 want = 0;                                                  /* the type of the first argument */
        id = (u8)(t - T_FN0);
        tp++;
        n = 0;
        if (id == 19) c_push();                                       /* TIMER */
        else if (id == 10) {                                          /* RND, RND(x) */
            skipsp();
            if (*tp == '(') {
                tp++;
                if (c_expr()) EM(C_TYPERR);
                n = 1;
                skipsp();
                if (*tp == ')') tp++; else cfail = 1;
            } else c_push();
        } else {
            skipsp();
            if (*tp != '(') { cfail = 1; return 0; }
            tp++;
            if (id == 11 || id == 12 || id == 13 || id >= 16) want = 1;
            if (c_expr() != want) EM(C_TYPERR);
            n = 1;
            if (id >= 16) {                                           /* LEFT$ RIGHT$ MID$ */
                skipsp();
                if (*tp != ',') { cfail = 1; return 1; }
                tp++;
                if (c_expr()) EM(C_TYPERR);
                n = 2;
                skipsp();
                if (*tp == ',') { tp++; if (c_expr()) EM(C_TYPERR); n = 3; }
            }
            skipsp();
            if (*tp == ')') tp++; else cfail = 1;
            cdep -= n - 1;
        }
        EM(C_FN); EM(id | ((u16)n << 8));
        return id >= 14 && id <= 18;
    }
    cfail = 1;
    return 0;
}

static u8 c_unary(void)
{
    u8 ty;
    skipsp();
    if (*tp == '-') { tp++; if (c_unary()) EM(C_TYPERR); EM(C_NEG); return 0; }
    if (*tp == '+') { tp++; return c_unary(); }
    ty = c_primary();
    skipsp();
    while (*tp == '^' && !cfail) { tp++; if (ty || c_primary()) EM(C_TYPERR); EM(C_POW); cdep--; ty = 0; skipsp(); }
    return ty;
}

static u8 c_term(void)
{
    u8 ty = c_unary();
    skipsp();
    while ((*tp == '*' || *tp == '/') && !cfail) {
        char op = *tp++;
        u16 *mark = cw;
        if (c_unary() || ty) EM(C_TYPERR);
        c_bin(op == '*' ? 0 : 1, mark);
        ty = 0;
        skipsp();
    }
    return ty;
}

static u8 c_sum(void)
{
    u8 ty = c_term(), tb;
    skipsp();
    while ((*tp == '+' || *tp == '-') && !cfail) {
        char op = *tp++;
        u16 *mark = cw;
        tb = c_term();
        if (ty && tb && op == '+') { EM(C_CAT); cdep--; }                  /* strings: + joins them */
        else {
            if (ty || tb) EM(C_TYPERR);
            c_bin(op == '+' ? 2 : 3, mark);
            ty = 0;
        }
        skipsp();
    }
    return ty;
}

static u8 c_compare(void)
{
    u8 op = 0, ty = c_sum();
    skipsp();
    if (*tp != '=' && *tp != '<' && *tp != '>') return ty;
    for (;;) {
        char ch = *tp;
        if (ch == '=') { op |= 2; tp++; }
        else if (ch == '<') { op |= 1; tp++; }
        else if (ch == '>') { op |= 4; tp++; }
        else break;
    }
    if (c_sum() != ty) EM(C_TYPERR);
    EM(ty ? C_SCMP : C_CMP); EM(op);
    cdep--;
    return 0;
}

static u8 c_not(void)
{
    skipsp();
    if (TK(tp) == T_NOT) { tp++; if (c_not()) EM(C_TYPERR); EM(C_NOT); return 0; }
    return c_compare();
}

static u8 c_and(void)
{
    u8 ty = c_not();
    skipsp();
    while (TK(tp) == T_AND && !cfail) { tp++; if (c_not() || ty) EM(C_TYPERR); EM(C_AND); cdep--; ty = 0; skipsp(); }
    return ty;
}

static u8 c_expr(void)
{
    u8 ty = c_and();
    skipsp();
    while (TK(tp) == T_OR && !cfail) { tp++; if (c_and() || ty) EM(C_TYPERR); EM(C_OR); cdep--; ty = 0; skipsp(); }
    return ty;
}

/* the machine: sp is the next free cell of the stack. (ysd-cc makes a chain of comparisons of a switch, so that the most frequent operations come first) */
#define VAR(o) (*(f32 *)((u8 *)numv + (o)))
static void run(const u16 *pc)
{
    f32 *sp = vv_f;
    u16 op;
    for (;;) {
        op = *pc++;
        if (op == C_NV) { *sp++ = VAR(*pc); pc++; continue; }
        if (op == C_MULV) { sp[-1] = f_mul(sp[-1], VAR(*pc)); pc++; if (sfp_err) goto err; continue; }
        if (op == C_NUM) { *sp++ = *(f32 *)*pc++; continue; }
        if (op == C_ADDV) { sp[-1] = f_add(sp[-1], VAR(*pc)); pc++; if (sfp_err) goto err; continue; }
        if (op == C_STV) { VAR(*pc) = sp[-1]; return; }                 /* the end of a LET statement: the variable gets the value */
        if (op == C_END) {
            vf = sp[-1];
            vs = CSTR(sp - 1);
            return;
        }
        if (op == C_SUB) { sp--; sp[-1] = f_sub(sp[-1], sp[0]); if (sfp_err) goto err; continue; }
        if (op == C_SUBV) { sp[-1] = f_sub(sp[-1], VAR(*pc)); pc++; if (sfp_err) goto err; continue; }
        if (op == C_MULN) { sp[-1] = f_mul(sp[-1], *(f32 *)*pc++); if (sfp_err) goto err; continue; }
        if (op == C_ADD) { sp--; sp[-1] = f_add(sp[-1], sp[0]); if (sfp_err) goto err; continue; }
        if (op == C_MUL) { sp--; sp[-1] = f_mul(sp[-1], sp[0]); if (sfp_err) goto err; continue; }
        if (op == C_CMP) {
            u8 m = (u8)*pc++;
            int c = f_cmp(sp[-2], sp[-1]);
            sp--;
            if ((c < 0 && (m & 1)) || (c == 0 && (m & 2)) || (c > 0 && (m & 4))) sp[-1] = F_SIGN | F_ONE; else sp[-1] = 0;       /* true is -1 */
            continue;
        }
        if (op == C_ADDN) { sp[-1] = f_add(sp[-1], *(f32 *)*pc++); if (sfp_err) goto err; continue; }
        if (op == C_SUBN) { sp[-1] = f_sub(sp[-1], *(f32 *)*pc++); if (sfp_err) goto err; continue; }
        switch (op) {
        case C_DIVV: sp[-1] = f_div(sp[-1], VAR(*pc)); pc++; if (sfp_err) goto err; break;
        case C_DIVN: sp[-1] = f_div(sp[-1], *(f32 *)*pc++); if (sfp_err) goto err; break;
        case C_DIV: sp--; sp[-1] = f_div(sp[-1], sp[0]); if (sfp_err) goto err; break;
        case C_NEG: sp[-1] = f_neg(sp[-1]); break;
        case C_SCMP: {
            u8 m = (u8)*pc++;
            int c = strcmp(CSTR(sp - 2), CSTR(sp - 1));
            sp--;
            if ((c < 0 && (m & 1)) || (c == 0 && (m & 2)) || (c > 0 && (m & 4))) sp[-1] = F_SIGN | F_ONE; else sp[-1] = 0;
            break;
        }
        case C_STR: {
            char *s = newtmp();
            const char *q = (const char *)*pc++;
            u8 n = 0;
            q++;
            while (*q && *q != '"') { if (n < STRL - 1) s[n++] = *q; q++; }
            s[n] = 0;
            CSTR(sp) = s;
            sp++;
            break;
        }
        case C_SV: CSTR(sp) = strv[*pc++]; sp++; break;
        case C_CAT: {
            u16 la, lb;
            char *s;
            sp--;
            la = (u16)strlen(CSTR(sp - 1));
            lb = (u16)strlen(CSTR(sp));
            if (la + lb >= STRL) { fail("String too long"); goto bad; }
            s = newtmp();
            memcpy(s, CSTR(sp - 1), la);
            memcpy(s + la, CSTR(sp), lb);
            s[la + lb] = 0;
            CSTR(sp - 1) = s;
            break;
        }
        case C_AV: {
            u16 w = *pc++;
            u8 nd = (u8)(w >> 8);
            s32 j = 0;
            sp -= nd;
            if (nd == 2) j = f_to_long(sp[1]);
            *sp = *elem_ij((u8)w, f_to_long(*sp), j);
            sp++;
            if (errmsg) goto bad;
            break;
        }
        case C_FN: {
            u16 w = *pc++;
            u8 n = (u8)(w >> 8);
            sp -= n;
            fn_apply((u8)w, n, sp);
            sp++;
            if (errmsg) goto bad;
            break;
        }
        case C_POW: {
            f32 x, y;
            sp--;
            x = sp[-1];
            y = sp[0];
            if (!x) { if (y) x = 0; else x = F_ONE; }
            else if (f_int(y) == y && f_cmp(f_abs(y), f_n(1000)) < 0) {
                s32 k = f_to_long(f_abs(y));
                f32 r = F_ONE, p = x;
                while (k) { if (k & 1) r = f_mul(r, p); k >>= 1; if (k) p = f_mul(p, p); }
                if (y & F_SIGN) r = f_div(F_ONE, r);
                x = r;
            } else if (x & F_SIGN) { fail("Illegal function call"); goto bad; }
#ifndef NOMATH
            else x = f_exp(f_mul(y, f_log(x)));
#else
            else { fail("Illegal function call"); goto bad; }
#endif
            sp[-1] = x;
            break;
        }
        case C_NOT: sp[-1] = f_from_long(~f_to_long(sp[-1])); break;
        case C_AND: sp--; sp[-1] = f_from_long(f_to_long(sp[-1]) & f_to_long(sp[0])); break;
        case C_OR: sp--; sp[-1] = f_from_long(f_to_long(sp[-1]) | f_to_long(sp[0])); break;
        default: fail("Type mismatch"); goto bad;                     /* C_TYPERR */
        }
    }
err:
    checkerr();
bad:
    vf = 0;
    vs = 0;
}

static void expr(void)
{
    char *p;
    cent_t *ce = 0;
    u8 again = 1, ty;
    if (errmsg) { setn(0); return; }
    skipsp();
    p = tp;
    if (INPOOL(p)) {
        ce = &cache[((u16)p >> 1) & (NCE - 1)];
        if (ce->p == p && ce->gen == cgen && ce->kind < 2) {
            tp = ce->end;
            vt = ce->kind;
            run(ce->code);
            return;
        }
    }
    for (;;) {                                                        /* compile it */
        cw = cfree;
        cdep = cfail = cfull = 0;
        tp = p;
        ty = c_expr();
        EM(C_END);
        if (cfail) { fail("Syntax error"); setn(0); return; }
        if (!cfull) break;
        if (!again) { fail("Out of memory"); setn(0); return; }
        again = 0;
        cflush();
        ce = 0;
    }
    if (ce) {
        ce->p = p; ce->end = tp; ce->code = cfree; ce->gen = cgen; ce->kind = ty;
    }
    vt = ty;
    run(cfree);
    if (ce) cfree = cw;
}

/* ---- program lines */
static u8 *findline(u16 n)                 /* first line with a number >= n, or pend */
{
    u8 *p = pool;
    while (p < pend && LNUM(p) < n) p = NEXTLINE(p);
    return p;
}

static f32 tbw[66];                        /* the line that is made by tokenize(): header and body, 250 bytes at the most */
#define tb ((u8 *)tbw)

static u8 kwlen;
static u8 kwfind(const char *s)            /* the token of the keyword or function at s (its length in kwlen), or 0 */
{
    const char *n = kwnames;
    u8 i;
    for (i = 0; i < NKW + NFN; i++) {
        const char *p = s, *q = n;
        if (i == NKW) n = q = fnames;
        while (*q && *p == *q) { p++; q++; }
        if (!*q) { kwlen = (u8)(p - s); return (u8)(i < NKW ? T_REM + i : T_FN0 + i - NKW); }
        n += strlen(n) + 1;
    }
    return 0;
}

/* the text of a line (already in upper case) to the internal form in tb: returns the size with the header, or 0 if it is too long */
static u8 tokenize(const char *s)
{
    u8 *d = tb + 3, *e = tb + 250;
    u8 lnmode = 0, t, k;
    char c;
    while ((c = *s) != 0) {
        if (d + 8 > e) return 0;
        if (c == ' ') { *d++ = ' '; s++; continue; }
        if ((lnmode == 1 && c == ',') || (lnmode == 2 && c == '-')) { *d++ = (u8)c; s++; continue; }
        if (c >= '0' && c <= '9' && lnmode) {
            u16 n = 0;
            while (*s >= '0' && *s <= '9') { n = (u16)(n * 10 + (*s - '0')); s++; }
            *d++ = T_LN; *d++ = (u8)(n >> 8); *d++ = (u8)n;
            continue;
        }
        lnmode = 0;
        if (c == '"') {                                    /* a string as typed */
            *d++ = (u8)c; s++;
            while (*s && *s != '"') { if (d + 4 > e) return 0; *d++ = (u8)*s++; }
            if (*s) *d++ = (u8)*s++;
            continue;
        }
        if (c == '\'' || c == '?') { *d++ = c == '?' ? T_QM : T_QREM; s++; if (c == '?') continue; goto rest; }
        if ((c >= '0' && c <= '9') || c == '.') {
            f32 f;
            const char *q = f_parse(s, &f);
            k = (u8)(q - s);
            if (k) {
                if (d + k + 8 > e) return 0;
                *d++ = T_NUM;
                                                           /* (the 6809 needs no alignment: the value follows the token) */
                *(f32 *)d = f;
                d += 4;
                *d++ = k;
                while (k--) *d++ = (u8)*s++;
                continue;
            }
        }
        if (isalpha_(c)) {
            t = kwfind(s);
            if (t && kwlen == 2 && isalpha_(s[2])) t = 0;          /* TO IF ON: not at the start of a name (TOTAL, ONE) */
            if (t) {
                *d++ = t;
                s += kwlen;
                if (t == T_GOTO || t == T_GOSUB || t == T_THEN || t == T_ELSE) lnmode = 1;
                else if (t == T_LIST || t == T_RUN) lnmode = 2;
                else if (t == T_REM || t == T_DATA) goto rest;
                continue;
            } else {
                u8 c1 = (u8)(c - 'A'), c2 = 0, l;
                const char *q = s + 1;
                char c2c = *q;
                u16 off;
                if (c2c >= '0' && c2c <= '9') c2 = (u8)(1 + (c2c - '0'));
                else if (isalpha_(c2c)) c2 = (u8)(11 + (c2c - 'A'));
                while (isalnum_(*q)) q++;
                l = (u8)(q - s);
                if (d + l + 8 > e) return 0;
                if (*q == '$') {                           /* A$ */
                    *d++ = T_SV; *d++ = c1;
                    if (l > 1) { *d++ = T_XN; *d++ = (u8)(l - 1); for (k = 1; k < l; k++) *d++ = (u8)s[k]; }
                    s = q + 1;
                    continue;
                } else {
                    const char *r = q;
                    while (*r == ' ') r++;
                    if (*r == '(') {                       /* A( */
                        *d++ = T_AV; *d++ = c1;
                        if (l > 1) { *d++ = T_XN; *d++ = (u8)(l - 1); for (k = 1; k < l; k++) *d++ = (u8)s[k]; }
                        s = q;
                        continue;
                    }
                }
                {
                    int sl = varslot((u16)((u16)c1 * 37 + c2));
                    if (sl < 0) return 0;                   /* too many variables */
                    off = (u16)((u16)sl * 4);
                }
                *d++ = T_NV; *d++ = (u8)(off >> 8); *d++ = (u8)off;
                if (l > 2) { *d++ = T_XN; *d++ = (u8)(l - 2); for (k = 2; k < l; k++) *d++ = (u8)s[k]; }
                s = q;
                continue;
            }
        }
        *d++ = (u8)c; s++;                                 /* anything else as typed */
        continue;
rest:                                                      /* the rest of the line as typed (REM, DATA) */
        while (*s) { if (d + 2 > e) return 0; *d++ = (u8)*s++; }
    }
    *d++ = 0;
    if ((d - tb) & 1) *d++ = 0;                            /* (the line is of an even size) */
    return (u8)(d - tb);
}

/* the internal form back to the text */
static void detok(const u8 *p, char *o)
{
    u8 t, k;
    const char *n;
    while ((t = *p) != 0) {
        p++;
        if (t < 0x80) {
            *o++ = (char)t;
            if (t == '"') { while (*p && *p != '"') *o++ = (char)*p++; if (*p) *o++ = (char)*p++; }
        } else if (t == T_NV) {
            u16 ix = vname[(u16)(((u16)p[0] << 8 | p[1]) >> 2)];
            u8 c2 = (u8)(ix % 37);
            *o++ = (char)('A' + ix / 37);
            if (c2) *o++ = (char)(c2 <= 10 ? '0' + c2 - 1 : 'A' + c2 - 11);
            p += 2;
        } else if (t == T_SV || t == T_AV) {
            *o++ = (char)('A' + *p++);
            if (*p == T_XN) { k = p[1]; p += 2; while (k--) *o++ = (char)*p++; }
            if (t == T_SV) *o++ = '$';
        } else if (t == T_XN) {
            k = *p++;
            while (k--) *o++ = (char)*p++;
        } else if (t == T_NUM) {
            k = p[4];
            p += 5;
            while (k--) *o++ = (char)*p++;
        } else if (t == T_LN) {
            utoa_((u16)((u16)p[0] << 8 | p[1]), o, 10);
            while (*o) o++;
            p += 2;
        } else if (t == T_QM) *o++ = '?';
        else if (t == T_QREM) { *o++ = '\''; while (*p) *o++ = (char)*p++; }
        else {
            n = t >= T_REM ? kwnames : fnames;
            k = (u8)(t >= T_REM ? t - T_REM : t - T_FN0);
            while (k--) n += strlen(n) + 1;
            while (*n) *o++ = *n++;
            if (t == T_REM || t == T_DATA) { while (*p) *o++ = (char)*p++; }
        }
    }
    *o = 0;
}

static void setline(u16 n, const char *text)      /* insert, replace or delete a line */
{
    u8 *p = findline(n);
    u16 len;
    cflush();
    if (p < pend && LNUM(p) == n) {
        u8 *q = NEXTLINE(p);
        memmove(p, q, (u16)(pend - q));
        pend -= q - p;
    }
    if (*text == 0) return;
    len = tokenize(text);
    if (!len || (u16)(pend - pool) + len > PGM) { fail("Out of memory"); return; }
    memmove(p + len, p, (u16)(pend - p));
    memcpy(p, tb, len);
    p[0] = (u8)(n >> 8);
    p[1] = (u8)n;
    p[2] = (u8)(len - 3);
    pend += len;
}

static void upcase(char *s)                /* upper case outside the quotes */
{
    u8 q = 0;
    for (; *s; s++) {
        if (*s == '"') q ^= 1;
        else if (!q && *s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    }
}

static void jump(u16 n)
{
    u8 *p = findline(n);
    if (p >= pend || LNUM(p) != n) { fail("Undefined line"); return; }
    cl = p;
    tp = LTEXT(p);
}

static u16 getlineno(void)
{
    u16 n;
    skipsp();
    if (TK(tp) != T_LN) { fail("Syntax error"); return 0; }
    n = OFS(tp);
    tp += 3;
    return n;
}

static void skiprest(void) { tp = (char *)NEXTLINE(cl) - 1; }              /* (the NUL at the end: there may be zeros in the numbers) */

static char *skiptok(char *p)              /* the token (or the character) at p and what belongs to it: the position after it */
{
    u8 t = TK(p);
    if (t == '"') { p++; while (*p && *p != '"') p++; return *p ? p + 1 : p; }
    if (t == T_NV || t == T_LN) return p + 3;
    if (t == T_SV || t == T_AV) return p + 2;
    if (t == T_XN) return p + 2 + (u8)p[1];
    if (t == T_NUM) { u8 *q = (u8 *)p + 1; return (char *)q + 5 + q[4]; }
    if (t == T_REM || t == T_QREM || t == T_DATA) { while (*p) p++; return p; }
    return p + 1;
}

/* ---- the statements */
static void st_print(void)
{
    char b[24];
    u8 nl = 1;
    for (;;) {
        skipsp();
        if (!*tp || *tp == ':' ) break;
        if (TK(tp) == T_ELSE) break;
        nl = 1;
        if (TK(tp) == T_TAB) { s32 n; tp++; n = getint(); if (*tp == ')') tp++; while (col < n && !errmsg) outc(' '); continue; }
        if (TK(tp) == T_SPC) { s32 n; tp++; n = getint(); if (*tp == ')') tp++; while (n-- > 0) outc(' '); continue; }
        if (*tp == ';') { tp++; nl = 0; continue; }
        if (*tp == ',') { tp++; nl = 0; do outc(' '); while (col % 14); continue; }
        expr();
        if (errmsg) return;
        if (vt) outs(vs);
        else { numstr(vf, b); outs(b); outc(' '); }
    }
    if (nl) outc('\n');
}

static void st_input(void)
{
    char line[LINEMAX], *p;
    int r;
    skipsp();
    if (*tp == '"') {
        expr();
        if (vt) outs(vs);
        skipsp();
        if (*tp == ';' || *tp == ',') tp++;
    } else outs("? ");
    flush();
    r = gets_(line, sizeof(line));
    col = 0;
    if (r < 0) { fail("End of input"); return; }
    upcase(line);
    p = line;
    for (;;) {
        skipsp();
        if (!lval()) { fail("Syntax error"); return; }
        {
            u8 isstr = lv_s != 0;
            f32 *dst = lv_f;
            char *sdst = lv_s;
            while (*p == ' ') p++;
            if (isstr) {
                u8 n = 0;
                while (*p && *p != ',' && n < STRL - 1) sdst[n++] = *p++;
                sdst[n] = 0;
            } else {
                u8 neg = 0;
                f32 f;
                if (*p == '-') { neg = 1; p++; } else if (*p == '+') p++;
                p = (char *)f_parse(p, &f);
                *dst = neg ? f_neg(f) : f;
            }
        }
        if (*p == ',') p++;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        break;
    }
}

static void st_let(void)
{
    f32 *dst;
    char *sdst = 0;
    if (TK(tp) == T_NV) { dst = (f32 *)((u8 *)numv + OFS(tp)); tp += 3; ext(); }      /* (the usual case: no call) */
    else {
        if (!lval()) { fail("Syntax error"); return; }
        dst = lv_f;
        sdst = lv_s;
    }
    skipsp();
    if (*tp != '=') { fail("Syntax error"); return; }
    tp++;
    expr();
    if (errmsg) return;
    if (sdst) {
        if (!vt) { fail("Type mismatch"); return; }
        strcpy(sdst, vs);
    } else {
        *dst = vnum();
    }
}

/* a LET statement of a number variable in the program: compiled as a whole (the value is stored by the code) and kept like an expression */
static void st_letc(void)
{
    char *p = tp;
    u16 off = OFS(tp);
    u8 ty;
    if (INPOOL(p)) {
        tp += 3;
        ext();
        skipsp();
        if (*tp == '=') {
            tp++;
            skipsp();
            cw = cfree;
            cdep = cfail = cfull = 0;
            ty = c_expr();
            if (ty) EM(C_TYPERR);
            EM(C_STV); EM(off);
            if (!cfail && !cfull) {
                cent_t *ce = &cache[((u16)p >> 1) & (NCE - 1)];
                ce->p = p; ce->end = tp; ce->code = cfree; ce->gen = cgen; ce->kind = 2;
                run(cfree);
                cfree = cw;
                return;
            }
            if (cfull) cflush();
        }
        tp = p;
    }
    st_let();                                         /* (everything else, and the errors) */
}

static void st_if(void)
{
    u8 t;
    char *p;
    expr();
    if (errmsg) return;
    t = vt ? 1 : (vf != 0 && F_EXP(vf) != 0);
    if (!eat(T_THEN) && !eat(T_GOTO)) { fail("Syntax error"); return; }
    if (t) {
        skipsp();
        if (TK(tp) == T_LN) jump(getlineno());
        return;                                   /* the statements after THEN follow */
    }
    for (p = tp; *p && TK(p) != T_ELSE; p = skiptok(p)) ;           /* look for ELSE */
    if (*p) {
        tp = p + 1;
        skipsp();
        if (TK(tp) == T_LN) jump(getlineno());
    } else skiprest();
}

static void st_for(void)
{
    forent *e;
    u16 ix;
    f32 lim, step = F_ONE, init;
    skipsp();
    if (TK(tp) != T_NV) { fail("Syntax error"); return; }
    ix = OFS(tp);
    tp += 3;
    ext();
    skipsp();
    if (*tp != '=') { fail("Syntax error"); return; }
    tp++;
    init = getnum();
    if (!eat(T_TO)) { fail("Syntax error"); return; }
    lim = getnum();
    if (eat(T_STEP)) step = getnum();
    if (errmsg) return;
    *(f32 *)((u8 *)numv + ix) = init;
    for (e = fors; e < fors + nfor; e++)
        if (e->vi == ix) { nfor = (u8)(e - fors); break; }
    if (nfor >= FORMAX) { fail("FOR nested too deep"); return; }
    e = &fors[nfor++];
    e->vi = ix;
    e->lim = lim;
    e->step = step;
    e->line = cl;
    e->tp = tp;
}

static void st_next(void)
{
    for (;;) {
        forent *e;
        int ix = -1;
        f32 v, *vp;
        int c;
        skipsp();
        if (TK(tp) == T_NV) { ix = (int)OFS(tp); tp += 3; ext(); }
        if (!nfor) { fail("NEXT without FOR"); return; }
        if (ix >= 0) {
            while (nfor && fors[nfor - 1].vi != (u16)ix) nfor--;
            if (!nfor) { fail("NEXT without FOR"); return; }
        }
        e = &fors[nfor - 1];
        vp = (f32 *)((u8 *)numv + e->vi);
        v = f_add(*vp, e->step);
        *vp = v;
        c = f_cmp(v, e->lim);
        if ((e->step & F_SIGN) ? c >= 0 : c <= 0) {
            cl = e->line;
            tp = e->tp;
            return;
        }
        nfor--;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_dim(void)
{
    for (;;) {
        u8 a;
        s32 d1, d2 = 0;
        u8 dim = 1;
        skipsp();
        if (TK(tp) != T_AV) { fail("Syntax error"); return; }
        a = (u8)tp[1];
        tp += 2;
        ext();
        skipsp();
        if (*tp != '(') { fail("Syntax error"); return; }
        tp++;
        d1 = getint();
        if (*tp == ',') { tp++; d2 = getint(); dim = 2; }
        if (*tp == ')') tp++; else { fail("Syntax error"); return; }
        if (d1 < 0 || d2 < 0) { fail("Illegal function call"); return; }
        dimarr(a, (u16)d1, (u16)d2, dim);
        if (errmsg) return;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_on(void)
{
    s32 n = getint();
    u8 sub;
    u8 *l;
    char *rest;
    if (eat(T_GOTO)) sub = 0; else if (eat(T_GOSUB)) sub = 1; else { fail("Syntax error"); return; }
    while (n > 1 && !errmsg) {                  /* skip n-1 line numbers */
        getlineno();
        skipsp();
        if (*tp == ',') tp++; else { return; }
        n--;
    }
    if (n < 1 || errmsg) { skiprest(); return; }
    {
        u16 target = getlineno();
        skipsp();
        while (*tp && *tp != ':') tp = skiptok(tp);         /* the rest of the list */
        l = cl;
        rest = tp;
        if (sub) {
            if (ngos >= GOSMAX) { fail("Out of memory"); return; }
            gos[ngos].line = l;
            gos[ngos].tp = rest;
            ngos++;
        }
        jump(target);
    }
}

static int data_next(f32 *f, char *s, u8 *isstr)   /* next DATA item: 0 if none */
{
    for (;;) {
        if (!dline) { dline = pool; dtp = 0; }
        if (!dtp) {
            for (;;) {
                char *p;
                if (dline >= pend) return 0;
                p = LTEXT(dline);
                while (*p == ' ') p++;
                if (TK(p) == T_DATA) { dtp = p + 1; break; }
                dline = NEXTLINE(dline);
            }
        }
        while (*dtp == ' ') dtp++;
        if (!*dtp) { dline = NEXTLINE(dline); dtp = 0; continue; }
        if (*dtp == ',') dtp++;
        while (*dtp == ' ') dtp++;
        if (!*dtp) { dline = NEXTLINE(dline); dtp = 0; continue; }
        if (*dtp == '"') {
            u8 n = 0;
            dtp++;
            while (*dtp && *dtp != '"' && n < STRL - 1) s[n++] = *dtp++;
            s[n] = 0;
            if (*dtp == '"') dtp++;
            *isstr = 1;
        } else {
            u8 neg = 0;
            char *q = dtp;
            u8 n = 0;
            while (*q && *q != ',') q++;
            while (n < STRL - 1 && dtp + n < q) { s[n] = dtp[n]; n++; }
            s[n] = 0;
            {
                const char *r = s;
                while (*r == ' ') r++;
                if (*r == '-') { neg = 1; r++; }
                f_parse(r, f);
                if (neg) *f = f_neg(*f);
            }
            dtp = q;
            *isstr = 0;
        }
        return 1;
    }
}

static void st_read(void)
{
    for (;;) {
        char s[STRL];
        f32 f = 0;
        u8 isstr;
        f32 *dst;
        char *sdst;
        skipsp();
        if (!lval()) { fail("Syntax error"); return; }
        dst = lv_f;
        sdst = lv_s;
        if (!data_next(&f, s, &isstr)) { fail("Out of DATA"); return; }
        if (sdst) strcpy(sdst, s); else *dst = f;
        skipsp();
        if (*tp == ',') { tp++; continue; }
        return;
    }
}

static void st_list(void)
{
    char lb[LINEMAX + 8];
    u16 a = 0, b = 0xFFFF;
    u8 *p;
    skipsp();
    if (TK(tp) == T_LN) {
        a = getlineno();
        b = a;
        skipsp();
        if (*tp == '-') {
            tp++;
            skipsp();
            b = (TK(tp) == T_LN) ? getlineno() : 0xFFFF;
        }
    } else if (*tp == '-') { tp++; b = getlineno(); }
    for (p = pool; p < pend; p = NEXTLINE(p)) {
        if (LNUM(p) < a || LNUM(p) > b) continue;
        outn(LNUM(p));
        outc(' ');
        detok((const u8 *)LTEXT(p), lb);
        outs(lb);
        outc('\n');
    }
}

/* an OS-9 path list ends with a CR, not with a zero */
static char pn[72];
static const char *pathof(const char *name)
{
    u8 n = 0;
    while (name[n] && n < sizeof(pn) - 2) { pn[n] = name[n]; n++; }
    pn[n] = '\r';
    pn[n + 1] = 0;
    return pn;
}

static void load_file(const char *name)
{
    int fd = sc_open(pathof(name), 1), r, i;
    static u8 buf[128];
    char line[LINEMAX];
    u16 len = 0;
    if (fd < 0) { failos("File not found"); return; }
    pend = pool;
    nvused = 0;
    cflush();
    clear_vars();
    while ((r = sc_read(fd, buf, sizeof(buf))) > 0) {
        for (i = 0; i < r; i++) {
            u8 c = buf[i];
            if (c == '\n' || c == '\r') {
                if (len) {
                    char *p = line;
                    u16 n = 0;
                    line[len] = 0;
                    upcase(line);
                    while (*p == ' ') p++;
                    while (*p >= '0' && *p <= '9') { n = (u16)(n * 10 + (*p - '0')); p++; }
                    if (*p == ' ') p++;
                    if (n) setline(n, p);
                }
                len = 0;
            } else if (len < LINEMAX - 1) line[len++] = (char)c;
        }
    }
    sc_close(fd);
}

static void save_file(const char *name)
{
    int fd;
    u8 *p;
    char b[LINEMAX + 12];
    name = pathof(name);
    fd = sc_open(name, 2);                  /* an old file is replaced */
    if (fd >= 0) { sc_close(fd); sc_delete(name); }
    fd = sc_create(name);
    if (fd < 0) { failos("Cannot create file"); return; }
    for (p = pool; p < pend; p = NEXTLINE(p)) {
        u16 n = LNUM(p), l;
        char t[8];
        utoa_(n, t, 10);
        strcpy(b, t);
        strcat(b, " ");
        detok((const u8 *)LTEXT(p), b + strlen(b));
        strcat(b, "\r");
        l = (u16)strlen(b);
        sc_write(fd, (u8 *)b, l);
    }
    sc_close(fd);
}

static void getname(char *dst)
{
    expr();
    if (errmsg) return;
    if (!vt) { fail("Type mismatch"); return; }
    strcpy(dst, vs);
}

/* one statement at tp */
static void st_gosub(void)
{
    u16 n = getlineno();
    if (errmsg) return;
    if (ngos >= GOSMAX) { fail("Out of memory"); return; }
    gos[ngos].line = cl;
    gos[ngos].tp = tp;
    ngos++;
    jump(n);
}

static void st_run(void)
{
    u16 n = 0;
    skipsp();
    if (TK(tp) == T_LN) n = getlineno();
    clear_vars();
    if (pend == pool) { running = 0; return; }
    if (n) jump(n); else { cl = pool; tp = LTEXT(pool); }
    running = 1;
}

/* (stmt_kw is called through a pointer: ysd-cc would put a function that has one call site into its caller, with its PUSHM of many registers) */
static void stmt_kw(void);
static void (*stmt_kw_p)(void);

static void stmt_kw(void)
{
    char name[STRL];
    u8 t = TK(tp);
    tp++;
    switch (t) {
    case T_QREM: case T_REM: case T_DATA: case T_ELSE: skiprest(); return;
    case T_QM: case T_PRINT: st_print(); return;
    case T_RETURN:
        if (!ngos) { fail("RETURN without GOSUB"); return; }
        ngos--;
        cl = gos[ngos].line;
        tp = gos[ngos].tp;
        return;
    case T_READ: st_read(); return;
    case T_RESTORE: dline = 0; dtp = 0; return;
    case T_RANDOMIZE: rseed ^= secs() * 2654435761UL; skiprest(); return;
    case T_RUN: st_run(); return;
    case T_LET: skipsp(); st_let(); return;
    case T_LIST: st_list(); return;
    case T_LOAD: getname(name); if (!errmsg) load_file(name); running = 0; return;
    case T_IF: st_if(); return;
    case T_INPUT: st_input(); return;
    case T_GOTO: jump(getlineno()); return;
    case T_GOSUB: st_gosub(); return;
    case T_FOR: st_for(); return;
    case T_NEXT: st_next(); return;
    case T_NEW: pend = pool; nvused = 0; cflush(); clear_vars(); running = 0; return;
    case T_END: case T_STOP: running = 0; return;
    case T_SAVE: getname(name); if (!errmsg) save_file(name); return;
    case T_SYSTEM: case T_BYE: case T_QUIT: bye = 1; running = 0; return;
    case T_DIM: st_dim(); return;
    case T_CLS: outs("\033[2J\033[H"); col = 0; return;
    case T_ON: st_on(); return;
    }
    fail("Syntax error");
}

/* run from (cl, tp) until the program ends */
static void execute(void)
{
    running = 1;
    errmsg = 0;
    sfp_err = 0;
    while (running && !errmsg) {
        u8 t;
        while (*tp == ' ') tp++;
        t = TK(tp);
        if (t == T_NV) {                                              /* A = ... : the code of the whole statement is kept */
            cent_t *ce = &cache[((u16)tp >> 1) & (NCE - 1)];
            if (ce->p == tp && ce->gen == cgen && ce->kind == 2) { tp = ce->end; run(ce->code); }
            else st_letc();
        } else if (t == T_NEXT) { tp++; st_next();
        } else if (t == T_IF) { tp++; st_if();
        } else if (t == T_GOTO) { tp++; jump(getlineno());
        } else if (t == T_FOR) { tp++; st_for();
        } else if (t == 0) {
            if (cl == (u8 *)dbuf) { running = 0; break; }
            cl = NEXTLINE(cl);
            if (cl >= pend) { running = 0; break; }
            tp = LTEXT(cl);
            continue;
        } else if (t == ':') { tp++; continue; }
        else if (t == T_SV || t == T_AV) st_let();
        else stmt_kw_p();
        if (sfp_err) checkerr();
    }
    if (errmsg) {
        if (col) outc('\n');
        outc('?');
        outs(errmsg);
        if (cl != (u8 *)dbuf && cl < pend) { outs(" in "); outn(LNUM(cl)); }
        outc('\n');
        errmsg = 0;
    }
    running = 0;
}

static void direct(char *line)
{
    char *p = line;
    u16 n = 0;
    upcase(line);
    while (*p == ' ') p++;
    if (!*p) return;
    if (*p >= '0' && *p <= '9') {
        while (*p >= '0' && *p <= '9') { n = (u16)(n * 10 + (*p - '0')); p++; }
        if (*p == ' ') p++;
        errmsg = 0;
        setline(n, p);
        clear_vars();
        if (errmsg) { outs("?"); outs(errmsg); outc('\n'); errmsg = 0; }
        return;
    }
    /* the direct mode line: header like a program line so that errors and jumps work */
    {
        u8 len = tokenize(p);
        if (!len) { outs("?Out of memory\n"); return; }
        memcpy(dbuf, tb, len);
        dbuf[0] = 0;
        dbuf[1] = 0;
        dbuf[2] = (char)(len - 3);
    }
    cl = (u8 *)dbuf;
    tp = dbuf + 3;
    execute();
}

int main(int argc, char **argv)
{
    char line[LINEMAX];
    stmt_kw_p = stmt_kw;
    cflush();
    pend = pool;
    clear_vars();
    if (argc > 1) {
        errmsg = 0;
        load_file(argv[1]);
        if (errmsg) { outs("?"); outs(errmsg); outc('\n'); flush(); return 1; }
        if (pend == pool) return 0;
        cl = pool;
        tp = LTEXT(pool);
        execute();
        flush();
        return 0;
    }
    outs("YSD BASIC " BASIC_VERSION "  (c) 2026 YUKARI Semiconductor Devices\n");
    {
        char t[24];
        u16 n = (u16)(PGM - (u16)(pend - pool));
        utoa_(n, t, 10);
        outs(t);
        outs(" bytes free\n");
    }
    while (!bye) {
        int r;
        if (col) outc('\n');
        outs("Ok\n");
        flush();
        r = gets_(line, sizeof(line));
        col = 0;                            /* the console echoed the line and its end */
        if (r < 0) break;
        direct(line);
        flush();
    }
    flush();
    return 0;
}
