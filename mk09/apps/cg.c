/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * cg - the back end of the C compiler of MK09: IR in (docs/IR.md), 6809 assembler text out (for `as`)
 *
 *   cg [-ptag] [-o out.s] file.ir     (tag: a prefix for the names of strings and static variables, different for each file of a program)
 *
 * Calling convention: the caller pushes the arguments from right to left (the first argument is nearest to the top), calls with JSR and
 * removes them itself; the result is in D; U is the frame pointer (the callee keeps it), X Y D are free.  A function with a frame of N bytes:
 *     pshs u / leau ,s / leas -N,s ... leas ,u / puls u,pc
 * the locals are at -N,u .. -1,u, the arguments at 4,u 6,u ...
 * Names in the assembler text: the C name with a leading _ (a function foo is _foo); labels L<n>, strings S<n>, static variables V<n>.
 *
 * The stack of the IR is simulated: each entry is a constant, an address, a value in D, a word that is in memory (not loaded yet) or a word on
 * the hardware stack.  Instructions use them directly (addd off,u / cmpd #5), so no code is made for a push that is not needed.
 * Invariant: the entries on the hardware stack (K_STK) are the lowest ones; all others (constants, addresses, memory words and at most one K_D)
 * are above them.  At a label or a jump everything is on the hardware stack.
 */
#include "ccio.h"

#define VS      40
#define NAMEM   18
#define NLABD   2400
#define LINEMAX 100

enum { K_CONST, K_ADDR, K_UADDR, K_D, K_STK, K_UW, K_GW, K_POST };
typedef struct ent {
    u8 k;
    int v;                  /* constant; offset of an address or of a memory word */
    int aux;                /* K_POST: the step */
    char nm[NAMEM];         /* K_ADDR, K_GW, K_POST: the name (with the _), "" for an absolute address (K_POST: "" = a local, v is its offset from u) */
} ent_t;
static ent_t vs[VS];
static u8 nvs;

static rfile in;
static wfile out;
static u8 errors;
static u16 line;
static char fname[120];
static u8 reachable;
static int frame;
static u16 nzl;                 /* local labels of the code generator: Z<n> */
static u8 depth_tab[NLABD];
#define NALIAS 400
static u16 alias_from[NALIAS], alias_to[NALIAS];      /* "alias A B" lines: a jump to A goes to B (found before the code is made) */
static u16 nalias;
static u8 helpers[10];
static char ptag[6];            /* a prefix for the names of strings and static variables (-p), different for each module that is linked */
static char *helper_name[10] = { "__mul", "__div", "__mod", "__udiv", "__umod", "__shl", "__shr", "__ushr", "__copy", 0 };

static void err(const char *msg)
{
    errors++;
    w_flush(&out);
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", fname, line, msg);
#else
    printf("%s:%u: %s\n", fname, line, msg);
#endif
}

/* ---------------------------------------------------------------- assembler text */
static void o_tab(const char *op)
{
    w_putc(&out, '\t');
    w_puts(&out, op);
}

static char *cat_num(char *d, int v)
{
    char t[7];
    u8 n = 0;
    u16 u;
    if (v < 0) { *d++ = '-'; u = (u16)(-v); } else u = (u16)v;
    do { t[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    while (n) *d++ = t[--n];
    *d = 0;
    return d;
}

static char *cat_str(char *d, const char *s)
{
    while (*s) *d++ = *s++;
    *d = 0;
    return d;
}

static u8 dz;       /* the flags describe D: 2 = N Z and V (V = 0) after ldd std sex;  1 = Z only after addd subd (V may be set);  0 = nothing */

/* one instruction: op and operand */
static void ins(const char *op, const char *arg)
{
    dz = (!strcmp(op, "ldd") || !strcmp(op, "std") || !strcmp(op, "sex")) ? 2 : (!strcmp(op, "addd") || !strcmp(op, "subd")) ? 1 : 0;
    o_tab(op);
    if (arg && *arg) { w_putc(&out, '\t'); w_puts(&out, arg); }
    w_putc(&out, '\n');
}

/* the operand "#n" */
static void ins_imm(const char *op, int n)
{
    char b[12];
    b[0] = '#';
    cat_num(b + 1, n);
    ins(op, b);
}

/* operand off,reg */
static void fmt_idx(char *b, int off, const char *reg)
{
    char *p = cat_num(b, off);
    *p++ = ',';
    cat_str(p, reg);
}

static void ins_idx(const char *op, int off, const char *reg)
{
    char b[16];
    fmt_idx(b, off, reg);
    ins(op, b);
}

/* operand name+off */
static void fmt_sym(char *b, const char *nm, int off)
{
    char *p = b;
    if (nm[0]) {
        p = cat_str(p, nm);
        if (off > 0) { *p++ = '+'; p = cat_num(p, off); }
        else if (off < 0) p = cat_num(p, off);
    } else p = cat_num(p, off);
    *p = 0;
}

static void use_helper(const char *n)
{
    u8 i;
    w_puts(&out, ";@use ");
    w_puts(&out, n);
    w_putc(&out, '\n');
    for (i = 0; helper_name[i]; i++) if (!strcmp(helper_name[i], n)) helpers[i] = 1;
}

/* the labels of the code are local to the function: @L<n> (a local label of the assembler) */
static u16 resolve(u16 l)
{
    u16 i;
    u8 guard = 0;
    for (i = 0; i < nalias && guard < 8; i++)
        if (alias_from[i] == l) { l = alias_to[i]; i = (u16)-1; guard++; }
    return l;
}

static void ins_lb(const char *cc, u16 lbl)
{
    char op[6], b[10];
    char *p;
    lbl = resolve(lbl);
    op[0] = 'j'; op[1] = 'b'; op[2] = 0;       /* JBxx: short when the label is near (the assembler decides) */
    strcat(op, cc);
    b[0] = '@'; b[1] = 'L';
    p = cat_num(b + 2, (int)lbl);
    *p = 0;
    ins(op, b);
}

static void def_label(u16 l)
{
    dz = 0;
    w_puts(&out, "@L");
    w_num(&out, (int)l);
    w_putc(&out, '\n');
}

/* ---------------------------------------------------------------- the stack of the IR */
static void push_ent(u8 k, int v, const char *nm)
{
    if (nvs >= VS) { err("the expression is too deep"); return; }
    vs[nvs].k = k;
    vs[nvs].aux = 0;
    vs[nvs].v = v;
    strcpy(vs[nvs].nm, nm);
    nvs++;
}

static void pop_n(u8 n) { nvs = n > nvs ? 0 : (u8)(nvs - n); }

/* the operand text of a word that can be used by an instruction directly (addd, cmpd, ldd): 0 if it cannot */
static u8 operand(const ent_t *e, char *b)
{
    switch (e->k) {
    case K_CONST: b[0] = '#'; cat_num(b + 1, e->v); return 1;
    case K_ADDR: b[0] = '#'; fmt_sym(b + 1, e->nm, e->v); return 1;
    case K_UW: fmt_idx(b, e->v, "u"); return 1;
    case K_GW: fmt_sym(b, e->nm, e->v); return 1;
    }
    return 0;
}

/* K_POST: "the word of the variable, which gets e->aux added when it is used" (p++ of a pointer or an int): its operand and the code that makes X the old value */
static void post_var(const ent_t *e, char *b)
{
    if (e->nm[0]) fmt_sym(b, e->nm, e->v); else fmt_idx(b, e->v, "u");
}

static void post_x(const ent_t *e)
{
    char b[24];
    post_var(e, b);
    ins("ldx", b);
    ins_idx("leax", e->aux, "x");
    ins("stx", b);
    ins_idx("leax", -e->aux, "x");
}

/* a pointer in X */
static void load_x(u8 i)
{
    ent_t *e = &vs[i];
    char b[24];
    switch (e->k) {
    case K_D: ins("tfr", "d,x"); break;
    case K_STK: ins("puls", "x"); break;
    case K_UADDR: ins_idx("leax", e->v, "u"); break;
    case K_POST: post_x(e); break;
    default: operand(e, b); ins("ldx", b); break;
    }
}

/* a word in D */
static void load_d(u8 i)
{
    ent_t *e = &vs[i];
    char b[24];
    switch (e->k) {
    case K_D: break;
    case K_STK: ins("puls", "d"); break;
    case K_POST: post_x(e); ins("tfr", "x,d"); break;
    case K_UADDR: ins_idx("leax", e->v, "u"); ins("tfr", "x,d"); break;
    case K_CONST:
        if (e->v == 0) { ins("clra", 0); ins("clrb", 0); }
        else ins_imm("ldd", e->v);
        break;
    default: operand(e, b); ins("ldd", b); break;
    }
}

/* push the entries from the lowest one that is not on the hardware stack up to i, in order (X is used, D is kept) */
static void push_upto(u8 i)
{
    u8 k = 0;
    char b[24];
    while (k < nvs && vs[k].k == K_STK) k++;
    for (; k <= i && k < nvs; k++) {
        ent_t *e = &vs[k];
        switch (e->k) {
        case K_D: ins("pshs", "d"); break;
        case K_POST: post_x(e); ins("pshs", "x"); break;
        case K_UADDR: ins_idx("leax", e->v, "u"); ins("pshs", "x"); break;
        case K_CONST: ins_imm("ldx", e->v); ins("pshs", "x"); break;
        default: operand(e, b); ins("ldx", b); ins("pshs", "x"); break;
        }
        e->k = K_STK;
    }
}

/* something is going to be computed into D: if an entry owns D, push it (and what is below it) */
static void free_d(void)
{
    u8 k;
    for (k = 0; k < nvs; k++) if (vs[k].k == K_D) { push_upto(k); return; }
}

/* memory words that are not loaded yet must be loaded (pushed) before memory changes: everything up to the highest one below `limit` */
static void flush_mem(u8 limit)
{
    u8 k, hi = 255;
    for (k = 0; k < limit; k++) if (vs[k].k == K_UW || vs[k].k == K_GW || vs[k].k == K_POST) hi = k;
    if (hi != 255) push_upto(hi);
}

/* the top of the stack in D (for ret, jz, calls); everything below stays */
static void top_to_d(void)
{
    ent_t *e = &vs[nvs - 1];
    if (e->k == K_D) return;
    free_d();
    load_d((u8)(nvs - 1));
    e->k = K_D;
}

/* all entries on the hardware stack (at a label or a jump) */
static void canonical(void)
{
    if (nvs) push_upto((u8)(nvs - 1));
}

/* ---------------------------------------------------------------- operations */
static const char *cc_true(const char *op)
{
    if (!strcmp(op, "eq")) return "eq";
    if (!strcmp(op, "ne")) return "ne";
    if (!strcmp(op, "lt")) return "lt";
    if (!strcmp(op, "le")) return "le";
    if (!strcmp(op, "gt")) return "gt";
    if (!strcmp(op, "ge")) return "ge";
    if (!strcmp(op, "ult")) return "lo";
    if (!strcmp(op, "ule")) return "ls";
    if (!strcmp(op, "ugt")) return "hi";
    return "hs";
}

static const char *cc_swap(const char *c)
{
    if (!strcmp(c, "lt")) return "gt";
    if (!strcmp(c, "gt")) return "lt";
    if (!strcmp(c, "le")) return "ge";
    if (!strcmp(c, "ge")) return "le";
    if (!strcmp(c, "lo")) return "hi";
    if (!strcmp(c, "hi")) return "lo";
    if (!strcmp(c, "ls")) return "hs";
    if (!strcmp(c, "hs")) return "ls";
    return c;
}

static const char *cc_not(const char *c)
{
    if (!strcmp(c, "eq")) return "ne";
    if (!strcmp(c, "ne")) return "eq";
    if (!strcmp(c, "lt")) return "ge";
    if (!strcmp(c, "ge")) return "lt";
    if (!strcmp(c, "le")) return "gt";
    if (!strcmp(c, "gt")) return "le";
    if (!strcmp(c, "lo")) return "hs";
    if (!strcmp(c, "hs")) return "lo";
    if (!strcmp(c, "ls")) return "hi";
    return "ls";
}

static u8 is_addr(const ent_t *e) { return e->k == K_ADDR || e->k == K_UADDR; }

static int fold_op(const char *op, int a, int b, u8 *ok)
{
    u16 ua = (u16)a, ub = (u16)b;
    *ok = 1;
    if (!strcmp(op, "add")) return a + b;
    if (!strcmp(op, "sub")) return a - b;
    if (!strcmp(op, "mul")) return a * b;
    if (!strcmp(op, "and")) return a & b;
    if (!strcmp(op, "or")) return a | b;
    if (!strcmp(op, "xor")) return a ^ b;
    if (!strcmp(op, "div")) { if (!b) { *ok = 0; return 0; } return a / b; }
    if (!strcmp(op, "mod")) { if (!b) { *ok = 0; return 0; } return a % b; }
    if (!strcmp(op, "udiv")) { if (!ub) { *ok = 0; return 0; } return (int)(ua / ub); }
    if (!strcmp(op, "umod")) { if (!ub) { *ok = 0; return 0; } return (int)(ua % ub); }
    if (!strcmp(op, "shl")) return b >= 16 ? 0 : (int)(u16)(ua << b);
    if (!strcmp(op, "ushr")) return b >= 16 ? 0 : (int)(ua >> b);
    if (!strcmp(op, "shr")) return b >= 16 ? (a < 0 ? -1 : 0) : a >> b;
    if (!strcmp(op, "eq")) return a == b;
    if (!strcmp(op, "ne")) return a != b;
    if (!strcmp(op, "lt")) return a < b;
    if (!strcmp(op, "le")) return a <= b;
    if (!strcmp(op, "gt")) return a > b;
    if (!strcmp(op, "ge")) return a >= b;
    if (!strcmp(op, "ult")) return ua < ub;
    if (!strcmp(op, "ule")) return ua <= ub;
    if (!strcmp(op, "ugt")) return ua > ub;
    if (!strcmp(op, "uge")) return ua >= ub;
    *ok = 0;
    return 0;
}

/* the second pair of an operand for the byte operations (anda/andb): hi and lo operand text; only for constants and memory words */
static u8 operand_bytes(const ent_t *e, char *hi, char *lo)
{
    if (e->k == K_CONST) {
        hi[0] = '#'; cat_num(hi + 1, (e->v >> 8) & 255);
        lo[0] = '#'; cat_num(lo + 1, e->v & 255);
        return 1;
    }
    if (e->k == K_UW) { fmt_idx(hi, e->v, "u"); fmt_idx(lo, e->v + 1, "u"); return 1; }
    if (e->k == K_GW) { fmt_sym(hi, e->nm, e->v); fmt_sym(lo, e->nm, e->v + 1); return 1; }
    return 0;
}

static void neg_d(void)
{
    ins("coma", 0);
    ins("comb", 0);
    ins_imm("addd", 1);
}

/* the result of a two-operand instruction is a K_D */
static void result_d(void)
{
    pop_n(2);
    push_ent(K_D, 0, "");
}

/* arithmetic and bit operations: add sub and or xor */
static void binop_d(const char *op)
{
    ent_t a = vs[nvs - 2], b = vs[nvs - 1];
    char ob[24], hi[24], lo[24];
    u8 add = !strcmp(op, "add"), sub = !strcmp(op, "sub");
    u8 bit = !add && !sub;
    const char *mn = "addd";
    const char *bm = !strcmp(op, "and") ? "and" : !strcmp(op, "or") ? "or" : "eor";
    u8 comm = !sub;
    char bm_a[5], bm_b[5];
    u8 bok;
    if (sub) mn = "subd";
    /* the left operand in D, the right one an operand of the instruction */
    if (bit) {
        strcpy(bm_a, bm); strcat(bm_a, "a");
        strcpy(bm_b, bm); strcat(bm_b, "b");
    }
    bok = b.k != K_D && (bit ? operand_bytes(&b, hi, lo) : operand(&b, ob));
    if (!bok) {
        /* b is in D, or b cannot be an operand: a is the one that is used as an operand if it can */
        if (b.k == K_D && comm && (bit ? operand_bytes(&a, hi, lo) : operand(&a, ob))) {
            if (bit) { ins(bm_a, hi); ins(bm_b, lo); } else ins(mn, ob);
            result_d();
            return;
        }
        if (b.k == K_D && sub && operand(&a, ob)) {
            neg_d();
            ins("addd", ob);
            result_d();
            return;
        }
        /* general: a on the hardware stack, b in D */
        push_upto((u8)(nvs - 2));
        top_to_d();
        if (bit) { ins(bm_a, ",s+"); ins(bm_b, ",s+"); }
        else if (sub) { neg_d(); ins("addd", ",s++"); }
        else ins("addd", ",s++");
        result_d();
        return;
    }
    /* b is an operand */
    if (a.k != K_D) {
        free_d();
        load_d((u8)(nvs - 2));
    }
    if (bit) { ins(bm_a, hi); ins(bm_b, lo); }
    else ins(mn, ob);
    result_d();
}

/* D shifted by a constant: inline */
static void shift_const(const char *op, int n)
{
    int i;
    if (!strcmp(op, "shl")) {
        if (n >= 8) { ins("tfr", "b,a"); ins("clrb", 0); n -= 8; }
        for (i = 0; i < n; i++) { ins("aslb", 0); ins("rola", 0); }
    } else if (!strcmp(op, "ushr")) {
        if (n >= 8) { ins("tfr", "a,b"); ins("clra", 0); n -= 8; }
        for (i = 0; i < n; i++) { ins("lsra", 0); ins("rorb", 0); }
    } else {
        if (n >= 8) { ins("tfr", "a,b"); ins("sex", 0); n -= 8; }
        for (i = 0; i < n; i++) { ins("asra", 0); ins("rorb", 0); }
    }
}

static int log2_of(int v)
{
    int k = 0;
    if (v <= 0 || (v & (v - 1))) return -1;
    while (v > 1) { v >>= 1; k++; }
    return k;
}

/* mul div mod udiv umod shl shr ushr */
static void binop_helper(const char *op)
{
    ent_t a = vs[nvs - 2], b = vs[nvs - 1];
    char hn[10];
    char ob[24];
    /* a constant shift count, or a multiplication by a power of two: inline */
    if (b.k == K_CONST && (!strcmp(op, "shl") || !strcmp(op, "shr") || !strcmp(op, "ushr"))) {
        if (a.k != K_D) { free_d(); load_d((u8)(nvs - 2)); }
        if (b.v >= 16 || b.v < 0) {
            if (!strcmp(op, "shr")) shift_const(op, 15);
            else { ins("clra", 0); ins("clrb", 0); }
        } else shift_const(op, b.v);
        result_d();
        return;
    }
    if (b.k == K_CONST && !strcmp(op, "mul") && log2_of(b.v) >= 0) {
        if (a.k != K_D) { free_d(); load_d((u8)(nvs - 2)); }
        shift_const("shl", log2_of(b.v));
        result_d();
        return;
    }
    if (b.k == K_CONST && !strcmp(op, "udiv") && log2_of(b.v) >= 0) {
        if (a.k != K_D) { free_d(); load_d((u8)(nvs - 2)); }
        shift_const("ushr", log2_of(b.v));
        result_d();
        return;
    }
    if (a.k == K_CONST && b.k != K_D && !strcmp(op, "mul") && log2_of(a.v) >= 0) {
        /* the entries are swapped: the same thing */
        vs[nvs - 2] = b;
        vs[nvs - 1] = a;
        binop_helper(op);
        return;
    }
    /* general: D = left, X = right */
    if (b.k == K_D) {
        ins("tfr", "d,x");
        vs[nvs - 1].k = K_CONST;        /* b is in X now: not an entry that owns D */
        load_d((u8)(nvs - 2));          /* a: a constant, memory or the hardware stack */
    } else if (b.k == K_STK) {
        ins("puls", "x");
        vs[nvs - 1].k = K_CONST;
        free_d();
        load_d((u8)(nvs - 2));
    } else {
        if (a.k != K_D) { free_d(); load_d((u8)(nvs - 2)); }
        if (b.k == K_UADDR) ins_idx("leax", b.v, "u");
        else { operand(&b, ob); ins("ldx", ob); }
    }
    hn[0] = '_'; hn[1] = '_';
    strcpy(hn + 2, op);
    ins("jbsr", hn);
    use_helper(hn);
    if (!strcmp(op, "mod") || !strcmp(op, "umod")) ins("tfr", "x,d");
    result_d();
}

/* a comparison; if the next instruction is jz or jnz it is combined with it: the result is returned as a condition (cc and true polarity) */
static void compare(const char *op, const char *jop, u16 jlabel)
{
    ent_t a = vs[nvs - 2], b = vs[nvs - 1];
    const char *cc = cc_true(op);
    char ob[24];
    u8 swapped = 0;
    if (jop && nvs > 2) { push_upto((u8)(nvs - 3)); a = vs[nvs - 2]; b = vs[nvs - 1]; }      /* (the pushes would change the flags: before the comparison) */
    if (b.k != K_D && operand(&b, ob)) {
        if (a.k != K_D) { free_d(); load_d((u8)(nvs - 2)); }
        if (b.k == K_CONST && b.v == 0 && dz && ((!strcmp(cc, "eq") || !strcmp(cc, "ne")) ||
                                                  (dz == 2 && (!strcmp(cc, "lt") || !strcmp(cc, "ge") || !strcmp(cc, "le") || !strcmp(cc, "gt"))))) {
            /* D is just loaded: the flags are those of the comparison with 0 */
        } else ins("cmpd", ob);
    } else if (b.k == K_D && operand(&a, ob)) {
        ins("cmpd", ob);
        swapped = 1;
    } else {
        push_upto((u8)(nvs - 2));
        top_to_d();
        ins("cmpd", ",s++");
        swapped = 1;
    }
    if (swapped) cc = cc_swap(cc);
    pop_n(2);
    if (jop) {
        if (!strcmp(jop, "jz")) cc = cc_not(cc);
        canonical();
        depth_tab[jlabel < NLABD ? jlabel : 0] = nvs;
        ins_lb(cc, jlabel);
        return;
    }
    {
        char l1[10], l2[10];
        char *p;
        char b1[8];
        u16 z = ++nzl;
        l1[0] = '@'; l1[1] = 'Z'; p = cat_num(l1 + 2, (int)z); *p = 0;
        l2[0] = '@'; l2[1] = 'Y'; p = cat_num(l2 + 2, (int)z); *p = 0;
        b1[0] = 'b'; strcpy(b1 + 1, cc);
        ins(b1, l1);
        ins("clra", 0);
        ins("clrb", 0);
        ins("bra", l2);
        w_puts(&out, l1); w_putc(&out, '\n');
        ins_imm("ldd", 1);
        w_puts(&out, l2); w_putc(&out, '\n');
        dz = 0;
        push_ent(K_D, 0, "");
    }
}

/* load and store */
static void do_load(u8 bytes, u8 uns)
{
    ent_t a = vs[nvs - 1];
    char b[24];
    if (a.k == K_POST) {                 /* *p++ : the pointer is used and changed in one go */
        char pv[24];
        free_d();
        post_var(&a, pv);
        ins("ldx", pv);
        if (bytes) {
            if (a.aux == 1) ins("ldb", ",x+"); else { ins("ldb", ",x"); ins_idx("leax", a.aux, "x"); }
        } else {
            if (a.aux == 2) ins("ldd", ",x++"); else { ins("ldd", ",x"); ins_idx("leax", a.aux, "x"); }
        }
        ins("stx", pv);
        if (bytes) { if (uns) ins("clra", 0); else ins("sex", 0); }
        vs[nvs - 1].k = K_D;
        return;
    }
    if (!bytes) {
        if (a.k == K_UADDR) { vs[nvs - 1].k = K_UW; return; }
        if (a.k == K_ADDR) { vs[nvs - 1].k = K_GW; return; }
        if (a.k == K_CONST) { vs[nvs - 1].k = K_GW; vs[nvs - 1].nm[0] = 0; return; }
        if (a.k == K_D) { ins("tfr", "d,x"); ins("ldd", ",x"); return; }
        free_d();
        load_x((u8)(nvs - 1));
        ins("ldd", ",x");
        vs[nvs - 1].k = K_D;
        return;
    }
    if (a.k == K_D) { ins("tfr", "d,x"); ins("ldb", ",x"); }
    else {
        free_d();
        if (a.k == K_UADDR) ins_idx("ldb", a.v, "u");
        else if (a.k == K_ADDR) { fmt_sym(b, a.nm, a.v); ins("ldb", b); }
        else if (a.k == K_CONST) { cat_num(b, a.v); ins("ldb", b); }
        else { load_x((u8)(nvs - 1)); ins("ldb", ",x"); }
    }
    if (uns) ins("clra", 0); else ins("sex", 0);
    vs[nvs - 1].k = K_D;
}

static void do_store(u8 bytes)
{
    ent_t a, v;
    const char *st = bytes ? "stb" : "std";
    char b[24];
    if (vs[nvs - 2].k == K_POST) {       /* *p++ = v */
        char pv[24];
        flush_mem((u8)(nvs - 2));
        a = vs[nvs - 2];
        if (vs[nvs - 1].k != K_D) { free_d(); load_d((u8)(nvs - 1)); }
        post_var(&a, pv);
        ins("ldx", pv);
        if (bytes && a.aux == 1) ins("stb", ",x+");
        else if (!bytes && a.aux == 2) ins("std", ",x++");
        else { ins(st, ",x"); ins_idx("leax", a.aux, "x"); }
        ins("stx", pv);
        pop_n(2);
        push_ent(K_D, 0, "");
        return;
    }
    flush_mem((u8)(nvs - 2));
    a = vs[nvs - 2];
    v = vs[nvs - 1];
    if (a.k == K_UADDR || a.k == K_ADDR || a.k == K_CONST) {
        if (v.k != K_D) { free_d(); load_d((u8)(nvs - 1)); }
        if (a.k == K_UADDR) ins_idx(st, a.v, "u");
        else if (a.k == K_ADDR) { fmt_sym(b, a.nm, a.v); ins(st, b); }
        else { cat_num(b, a.v); ins(st, b); }
    } else {
        /* the pointer goes to X, the value to D */
        if (v.k == K_D) load_x((u8)(nvs - 2));
        else if (v.k == K_STK) {
            ins("puls", "d");
            vs[nvs - 1].k = K_CONST;
            load_x((u8)(nvs - 2));
        } else {
            if (a.k == K_D) { ins("tfr", "d,x"); vs[nvs - 2].k = K_CONST; }
            else {
                free_d();
                load_x((u8)(nvs - 2));
                vs[nvs - 2].k = K_CONST;
            }
            free_d();
            load_d((u8)(nvs - 1));
        }
        ins(st, ",x");
    }
    pop_n(2);
    push_ent(K_D, 0, "");
}

/* ---------------------------------------------------------------- reading the IR */
static char buf[LINEMAX + 2], nbuf[LINEMAX + 2];
static u8 have_next;

static u8 read_raw(char *dst)
{
    u16 n = 0;
    int c;
    for (;;) {
        c = r_getc(&in);
        if (c < 0) { if (!n) return 0; break; }
        if (c == '\n') break;
        if (c == '\r') continue;
        if (n < LINEMAX) dst[n++] = (char)c;
    }
    dst[n] = 0;
    return 1;
}

/* the next line that is not empty or a comment (trailing comments are cut) */
static u8 next_line(char *dst)
{
    for (;;) {
        char *s, *sc;
        if (!read_raw(dst)) return 0;
        line++;
        sc = strchr(dst, ';');
        if (sc) {
            /* a ; inside a string of a .s line is not a comment: those lines start with .s */
            s = dst;
            while (*s == ' ' || *s == '\t') s++;
            if (strncmp(s, ".s ", 3)) *sc = 0;
        }
        s = dst;
        while (*s == ' ' || *s == '\t') s++;
        if (*s) {
            if (s != dst) memmove(dst, s, strlen(s) + 1);
            return 1;
        }
    }
}

static u8 get_line(void)
{
    if (have_next) { strcpy(buf, nbuf); have_next = 0; return 1; }
    return next_line(buf);
}

static const char *peek_line(void)
{
    if (!have_next) {
        if (!next_line(nbuf)) return NULL;
        have_next = 1;
    }
    return nbuf;
}

static char *field(char *s, u8 n)
{
    /* the n-th (from 0) blank separated word of s; modifies s */
    u8 i;
    for (i = 0; i < n; i++) {
        while (*s && *s != ' ' && *s != '\t') s++;
        while (*s == ' ' || *s == '\t') s++;
    }
    return s;
}

static int to_int(const char *s)
{
    int v = 0;
    u8 neg = 0;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    return neg ? -v : v;
}

static u16 label_num(const char *s)
{
    return (u16)to_int(s + (s[0] == 'L' ? 1 : 0));
}

/* mangling: foo -> _foo; the compiler's own names (L12 S3 V4) stay as they are for labels, data and statics */
static void mangle(char *d, const char *s)
{
    d[0] = '_';
    strcpy(d + 1, s);
}

static void data_name(char *dst, const char *s);

/* ---------------------------------------------------------------- data */
static void data_line(char *l)
{
    char *op = l;
    char *arg = field(l, 1);
    char opb[8];
    u8 n = 0;
    while (*l && *l != ' ' && *l != '\t' && n < 7) opb[n++] = *l++;
    opb[n] = 0;
    (void)op;
    if (!strcmp(opb, ".b")) { o_tab("fcb\t"); w_puts(&out, arg); w_putc(&out, '\n'); }
    else if (!strcmp(opb, ".w")) { o_tab("fdb\t"); w_puts(&out, arg); w_putc(&out, '\n'); }
    else if (!strcmp(opb, ".z")) { o_tab("rmb\t"); w_puts(&out, arg); w_putc(&out, '\n'); }
    else if (!strcmp(opb, ".wa")) {
        char raw[NAMEM + 2], nm[NAMEM + 2];
        const char *p = arg;
        u8 i = 0;
        while (*p && *p != '+' && *p != '-' && i < NAMEM) raw[i++] = *p++;
        raw[i] = 0;
        data_name(nm, raw);
        w_puts(&out, ";@use "); w_puts(&out, nm); w_putc(&out, '\n');
        o_tab("fdb\t");
        w_puts(&out, nm);
        w_puts(&out, p);
        w_putc(&out, '\n');
    } else if (!strcmp(opb, ".s")) {
        /* ".s "text" with \xHH escapes": bytes */
        const char *p = arg;
        u8 cnt = 0;
        if (*p == '"') p++;
        while (*p && !(*p == '"' && p[1] == 0)) {
            int v;
            if (*p == '\\') {
                if (p[1] == 'x') {
                    u8 h = 0, k;
                    v = 0;
                    for (k = 0; k < 2; k++) {
                        char c = p[2 + k];
                        h = (u8)(c <= '9' ? c - '0' : (c | 32) - 'a' + 10);
                        v = v * 16 + h;
                    }
                    p += 4;
                } else { v = (u8)p[1]; p += 2; }
            } else { v = (u8)*p; p++; }
            if (cnt == 0) o_tab("fcb\t"); else w_putc(&out, ',');
            w_num(&out, v);
            cnt++;
            if (cnt == 12) { w_putc(&out, '\n'); cnt = 0; }
        }
        if (cnt) w_putc(&out, '\n');
    } else err("bad data line");
}

static void data_name(char *dst, const char *s)
{
    if ((s[0] == 'S' || s[0] == 'V') && s[1] >= '0' && s[1] <= '9') { strcpy(dst, ptag); strcat(dst, s); }
    else mangle(dst, s);
}

/* ---------------------------------------------------------------- functions */
static void function(char *hdr)
{
    char name[NAMEM + 2];
    char nm[NAMEM + 2];
    char *f1 = field(hdr, 1), *f2;
    u8 i = 0;
    while (*f1 && *f1 != ' ' && i < NAMEM) nm[i++] = *f1++;
    nm[i] = 0;
    f2 = field(hdr, 2);
    frame = to_int(f2);
    mangle(name, nm);
    w_puts(&out, ";@def ");
    w_puts(&out, name);
    w_putc(&out, '\n');
    w_puts(&out, name);
    w_putc(&out, '\n');
    ins("pshs", "u");
    ins("leau", ",s");
    if (frame) ins_idx("leas", -frame, "s");
    nvs = 0;
    reachable = 1;
    for (;;) {
        char *op, *a1;
        u8 n;
        char opb[12];
        if (!get_line()) { err("the file ends in a function"); return; }
        if (!strcmp(buf, ".end")) break;
        if (buf[0] == 'L' && buf[strlen(buf) - 1] == ':') {
            /* a label */
            u16 l;
            buf[strlen(buf) - 1] = 0;
            l = label_num(buf);
            if (reachable) { canonical(); depth_tab[l < NLABD ? l : 0] = nvs; }
            else {
                u8 d = depth_tab[l < NLABD ? l : 0];
                nvs = 0;
                while (nvs < d) push_ent(K_STK, 0, "");
            }
            reachable = 1;
            def_label(l);
            continue;
        }
        op = buf;
        n = 0;
        while (*op && *op != ' ' && *op != '\t' && n < 11) opb[n++] = *op++;
        opb[n] = 0;
        a1 = field(buf, 1);
        if (!strcmp(opb, "alias")) continue;       /* handled by the first pass over the file (prescan) */
        if (!reachable) continue;
        if (!strcmp(opb, "const")) push_ent(K_CONST, to_int(a1), "");
        else if (!strcmp(opb, "addr")) {
            char nmx[NAMEM + 2];
            char raw[NAMEM + 2];
            u8 k = 0;
            int off = 0;
            while (*a1 && *a1 != '+' && *a1 != '-' && k < NAMEM) raw[k++] = *a1++;
            raw[k] = 0;
            if (*a1 == '+' || *a1 == '-') off = to_int(a1);
            data_name(nmx, raw);
            w_puts(&out, ";@use "); w_puts(&out, nmx); w_putc(&out, '\n');
            push_ent(K_ADDR, off, nmx);
        }
        else if (!strcmp(opb, "laddr")) push_ent(K_UADDR, to_int(a1) - frame, "");
        else if (!strcmp(opb, "paddr")) push_ent(K_UADDR, 4 + 2 * to_int(a1), "");
        else if (!strcmp(opb, "load.w")) do_load(0, 0);
        else if (!strcmp(opb, "load.b")) do_load(1, 0);
        else if (!strcmp(opb, "load.ub")) do_load(1, 1);
        else if (!strcmp(opb, "store.w")) do_store(0);
        else if (!strcmp(opb, "store.b")) do_store(1);
        else if (!strcmp(opb, "postinc")) {
            ent_t t = vs[nvs - 1];
            int k = to_int(a1);
            if (t.k == K_UADDR || t.k == K_ADDR) {
                vs[nvs - 1].k = K_POST;
                vs[nvs - 1].aux = k;
            } else {                      /* any address: X = a;  D = old; the word += k */
                free_d();
                load_x((u8)(nvs - 1));
                ins("ldd", ",x");
                ins("pshs", "d");
                ins_imm("addd", k);
                ins("std", ",x");
                ins("puls", "d");
                vs[nvs - 1].k = K_D;
            }
        }
        else if (!strcmp(opb, "dup")) {
            ent_t t = vs[nvs - 1];
            if (t.k == K_POST) { free_d(); load_d((u8)(nvs - 1)); vs[nvs - 1].k = K_D; t = vs[nvs - 1]; }
            if (t.k == K_D) { ins("pshs", "d"); vs[nvs - 1].k = K_STK; push_ent(K_D, 0, ""); }
            else if (t.k == K_STK) { ins("ldx", ",s"); ins("pshs", "x"); push_ent(K_STK, 0, ""); }
            else push_ent(t.k, t.v, t.nm);
        }
        else if (!strcmp(opb, "drop")) {
            if (nvs && vs[nvs - 1].k == K_POST) post_x(&vs[nvs - 1]);       /* (the variable is still changed) */
            if (nvs && vs[nvs - 1].k == K_STK) ins("leas", "2,s");
            pop_n(1);
        }
        else if (!strcmp(opb, "swap")) {
            ent_t t = vs[nvs - 2];
            if (vs[nvs - 1].k != K_STK && vs[nvs - 2].k != K_STK && !(vs[nvs - 1].k == K_D && vs[nvs - 2].k == K_D)) {
                vs[nvs - 2] = vs[nvs - 1];
                vs[nvs - 1] = t;
            } else {
                push_upto((u8)(nvs - 1));
                ins("puls", "d"); ins("puls", "x"); ins("pshs", "d"); ins("pshs", "x");
            }
        }
        else if (!strcmp(opb, "add") || !strcmp(opb, "sub") || !strcmp(opb, "and") || !strcmp(opb, "or") || !strcmp(opb, "xor") ||
                 !strcmp(opb, "mul") || !strcmp(opb, "div") || !strcmp(opb, "mod") || !strcmp(opb, "udiv") || !strcmp(opb, "umod") ||
                 !strcmp(opb, "shl") || !strcmp(opb, "shr") || !strcmp(opb, "ushr")) {
            ent_t a = vs[nvs - 2], b = vs[nvs - 1];
            u8 ok;
            if (a.k == K_CONST && b.k == K_CONST) {
                int v = fold_op(opb, a.v, b.v, &ok);
                if (ok) { pop_n(2); push_ent(K_CONST, v, ""); continue; }
            }
            if ((!strcmp(opb, "add") || !strcmp(opb, "sub")) && b.k == K_CONST && is_addr(&a)) {
                vs[nvs - 2].v = a.v + (opb[0] == 'a' ? b.v : -b.v);
                pop_n(1);
                continue;
            }
            if (!strcmp(opb, "add") && a.k == K_CONST && is_addr(&b)) {
                vs[nvs - 1].v = b.v + a.v;
                vs[nvs - 2] = vs[nvs - 1];
                pop_n(1);
                continue;
            }
            if (!strcmp(opb, "add") && a.k == K_CONST && a.v == 0) { vs[nvs - 2] = b; pop_n(1); continue; }
            if ((!strcmp(opb, "add") || !strcmp(opb, "sub")) && b.k == K_CONST && b.v == 0) { pop_n(1); continue; }
            if (!strcmp(opb, "add") || !strcmp(opb, "sub") || !strcmp(opb, "and") || !strcmp(opb, "or") || !strcmp(opb, "xor")) binop_d(opb);
            else binop_helper(opb);
        }
        else if (!strcmp(opb, "eq") || !strcmp(opb, "ne") || !strcmp(opb, "lt") || !strcmp(opb, "le") || !strcmp(opb, "gt") || !strcmp(opb, "ge") ||
                 !strcmp(opb, "ult") || !strcmp(opb, "ule") || !strcmp(opb, "ugt") || !strcmp(opb, "uge")) {
            ent_t a = vs[nvs - 2], b = vs[nvs - 1];
            u8 ok;
            const char *nx;
            if (a.k == K_CONST && b.k == K_CONST) {
                int v = fold_op(opb, a.v, b.v, &ok);
                pop_n(2);
                push_ent(K_CONST, v, "");
                continue;
            }
            nx = peek_line();
            if (nx && (!strncmp(nx, "jz ", 3) || !strncmp(nx, "jnz ", 4))) {
                char jl[LINEMAX + 2];
                char jop[4];
                strcpy(jl, nx);
                have_next = 0;
                jop[0] = 'j'; jop[1] = 'z'; jop[2] = 0;
                if (jl[1] == 'n') { jop[1] = 'n'; jop[2] = 'z'; jop[3] = 0; }
                compare(opb, jop, label_num(field(jl, 1)));
                /* the rest of the stack goes to the label: the depth is recorded by compare */
            } else compare(opb, 0, 0);
        }
        else if (!strcmp(opb, "neg") || !strcmp(opb, "com") || !strcmp(opb, "not") || !strcmp(opb, "sx") || !strcmp(opb, "zx")) {
            ent_t a = vs[nvs - 1];
            if (a.k == K_CONST) {
                int v = a.v;
                if (!strcmp(opb, "neg")) v = -v;
                else if (!strcmp(opb, "com")) v = ~v;
                else if (!strcmp(opb, "not")) v = !v;
                else if (!strcmp(opb, "sx")) v = (int)(s8)v;
                else v = v & 255;
                vs[nvs - 1].v = v;
                continue;
            }
            top_to_d();
            if (!strcmp(opb, "neg")) neg_d();
            else if (!strcmp(opb, "com")) { ins("coma", 0); ins("comb", 0); }
            else if (!strcmp(opb, "sx")) ins("sex", 0);
            else if (!strcmp(opb, "zx")) ins("clra", 0);
            else {
                u16 z = ++nzl;
                char l1[10], l2[10];
                char *p;
                l1[0] = '@'; l1[1] = 'Z'; p = cat_num(l1 + 2, (int)z); *p = 0;
                l2[0] = '@'; l2[1] = 'Y'; p = cat_num(l2 + 2, (int)z); *p = 0;
                ins_imm("cmpd", 0);
                ins("beq", l1);
                ins("clra", 0);
                ins("clrb", 0);
                ins("bra", l2);
                w_puts(&out, l1); w_putc(&out, '\n');
                ins_imm("ldd", 1);
                w_puts(&out, l2); w_putc(&out, '\n');
                dz = 0;
            }
        }
        else if (!strcmp(opb, "jmp")) {
            u16 l = label_num(a1);
            canonical();
            depth_tab[l < NLABD ? l : 0] = nvs;
            ins_lb("ra", l);
            reachable = 0;
        }
        else if (!strcmp(opb, "jz") || !strcmp(opb, "jnz")) {
            u16 l = label_num(a1);
            ent_t t = vs[nvs - 1];
            if (t.k == K_CONST) {
                u8 take = (opb[1] == 'z') ? (t.v == 0) : (t.v != 0);
                pop_n(1);
                if (take) { canonical(); depth_tab[l < NLABD ? l : 0] = nvs; ins_lb("ra", l); reachable = 0; }
                continue;
            }
            if (nvs > 1) push_upto((u8)(nvs - 2));      /* (the pushes would change the flags: before the comparison) */
            top_to_d();
            if (!dz) ins_imm("cmpd", 0);       /* (ldd addd subd std sex have set Z) */
            pop_n(1);
            canonical();
            depth_tab[l < NLABD ? l : 0] = nvs;
            ins_lb(opb[1] == 'z' ? "eq" : "ne", l);
        }
        else if (!strcmp(opb, "call") || !strcmp(opb, "calli")) {
            u8 ind = opb[4] == 'i';
            char nmx[NAMEM + 2];
            int cnt;
            char raw[NAMEM + 2];
            u8 k = 0;
            char *a2;
            if (!ind) {
                a2 = field(buf, 2);
                cnt = to_int(a2);
                while (*a1 && *a1 != ' ' && k < NAMEM) raw[k++] = *a1++;
                raw[k] = 0;
                mangle(nmx, raw);
                w_puts(&out, ";@use "); w_puts(&out, nmx); w_putc(&out, '\n');
            } else { cnt = to_int(a1); nmx[0] = 0; }
            if (nvs) push_upto((u8)(nvs - 1));
            if (ind) {
                char b[12];
                fmt_idx(b + 1, 2 * cnt, "s");
                b[0] = '[';
                { u8 l = (u8)strlen(b); b[l] = ']'; b[l + 1] = 0; }
                ins("jsr", b);
                if (cnt >= 0) ins_idx("leas", 2 * cnt + 2, "s");
                pop_n((u8)(cnt + 1));
            } else {
                ins("jbsr", nmx);
                if (cnt) ins_idx("leas", 2 * cnt, "s");
                pop_n((u8)cnt);
            }
            push_ent(K_D, 0, "");
        }
        else if (!strcmp(opb, "ret")) {
            top_to_d();
            ins("leas", ",u");
            ins("puls", "u,pc");
            nvs = 0;
            reachable = 0;
        }
        else if (!strcmp(opb, "copy")) {
            int n = to_int(a1);
            push_upto((u8)(nvs - 1));
            ins("ldx", ",s");
            ins("ldy", "2,s");
            ins_imm("ldd", n);
            ins("jbsr", "__copy");
            use_helper("__copy");
            ins("leas", "2,s");
            pop_n(2);
            push_ent(K_STK, 0, "");
        }
        else { err("unknown IR instruction"); }
        if (nvs > VS) { err("stack overflow in the code generator"); nvs = 0; }
    }
    if (reachable) {
        ins("clra", 0);
        ins("clrb", 0);
        ins("leas", ",u");
        ins("puls", "u,pc");
    }
    reachable = 0;
    nvs = 0;
}

static void toplevel(void)
{
    while (get_line()) {
        char *op = buf;
        char opb[10];
        u8 n = 0;
        while (*op && *op != ' ' && *op != '\t' && n < 9) opb[n++] = *op++;
        opb[n] = 0;
        if (!strcmp(opb, ".extern")) {
            char nm[NAMEM + 2], nmx[NAMEM + 2];
            u8 i = 0;
            char *a = field(buf, 1);
            while (*a && i < NAMEM) nm[i++] = *a++;
            nm[i] = 0;
            mangle(nmx, nm);
            w_puts(&out, ";@ext ");
            w_puts(&out, nmx);
            w_putc(&out, '\n');
        } else if (!strcmp(opb, ".func")) function(buf);
        else if (!strcmp(opb, ".bss")) {
            char nm[NAMEM + 2], nmx[NAMEM + 2];
            u8 i = 0;
            char *a = field(buf, 1);
            int size;
            while (*a && *a != ' ' && i < NAMEM) nm[i++] = *a++;
            nm[i] = 0;
            size = to_int(field(buf, 2));
            data_name(nmx, nm);
            w_puts(&out, ";@def ");
            w_puts(&out, nmx);
            w_putc(&out, '\n');
            ins("bss", 0);
            w_puts(&out, nmx);
            w_puts(&out, "\trmb\t");
            w_num(&out, size);
            w_putc(&out, '\n');
            ins("code", 0);
        } else if (!strcmp(opb, ".data")) {
            char nm[NAMEM + 2], nmx[NAMEM + 2];
            u8 i = 0;
            char *a = field(buf, 1);
            while (*a && *a != ' ' && i < NAMEM) nm[i++] = *a++;
            nm[i] = 0;
            data_name(nmx, nm);
            w_puts(&out, ";@def ");
            w_puts(&out, nmx);
            w_putc(&out, '\n');
            w_puts(&out, nmx);
            w_putc(&out, '\n');
            for (;;) {
                if (!get_line()) { err("the file ends in a data item"); return; }
                if (!strcmp(buf, ".end")) break;
                data_line(buf);
            }
        } else err("unknown directive");
    }
    {
        u8 i;
        for (i = 0; helper_name[i]; i++)
            if (helpers[i]) { w_puts(&out, ";@ext "); w_puts(&out, helper_name[i]); w_putc(&out, '\n'); }
    }
}

/* the first pass over the IR: the aliases of labels */
static void prescan(void)
{
    char l[LINEMAX + 2];
    while (read_raw(l)) {
        char *p = l;
        while (*p == ' ' || *p == '\t') p++;
        if (!strncmp(p, "alias ", 6)) {
            char *a = field(p, 1), *b = field(p, 2);
            if (nalias >= NALIAS) { err("too many label aliases"); return; }
            alias_from[nalias] = label_num(a);
            alias_to[nalias] = label_num(b);
            nalias++;
        }
    }
}

/* ---------------------------------------------------------------- the library (option -L)
 * The assembler text of a program is  start code + the code of the program + the routines of the library that it needs.  The library files
 * (/LIB/CLIB.S, /LIB/RT.S) are in blocks: one or more lines ;@def name, then the text, up to the next ;@def after some text.  A ;@use name or ;@ext name line
 * says that the text uses a name.  The blocks that define a needed name are copied; their own needs are looked for in turn. */
#define NNAME   150
#define POOL    2200
#define NLIB    2
static char *libs[NLIB] = { "/LIB/CLIB.S", "/LIB/RT.S" };
static char pool[POOL];
static u16 plen;
static char *names[NNAME];
static u8 state[NNAME];         /* 0 needed, 1 defined by the program or taken from the library */
static u8 nnames;

static int find_name(const char *s, u16 n)
{
    u8 i;
    for (i = 0; i < nnames; i++) if (!strncmp(names[i], s, n) && names[i][n] == 0) return i;
    return -1;
}

static int add_name(const char *s, u16 n, u8 st)
{
    int k = find_name(s, n);
    if (k >= 0) {
        if (st) state[k] = 1;
        return k;
    }
    if (nnames >= NNAME || plen + n + 1 > POOL) { err("too many names in the library"); return -1; }
    memcpy(pool + plen, s, n);
    pool[plen + n] = 0;
    names[nnames] = pool + plen;
    plen += n + 1;
    state[nnames] = st;
    return nnames++;
}

/* ";@def name" (1), ";@use name" or ";@ext name" (2); 0 if the line has no marker */
static u8 marker(const char *l, const char **name, u16 *n)
{
    u8 kind;
    if (l[0] != ';' || l[1] != '@') return 0;
    if (!strncmp(l + 2, "def ", 4)) kind = 1;
    else if (!strncmp(l + 2, "use ", 4) || !strncmp(l + 2, "ext ", 4)) kind = 2;
    else return 0;
    l += 6;
    *name = l;
    *n = 0;
    while (l[*n] && l[*n] != ' ' && l[*n] != '\r') (*n)++;
    return kind;
}

static u8 text_line(rfile *r, char *l)
{
    u8 n = 0;
    int c;
    for (;;) {
        c = r_getc(r);
        if (c < 0) { if (!n) return 0; break; }
        if (c == '\n') break;
        if (n < 100) l[n++] = (char)c;
    }
    l[n] = 0;
    return 1;
}

static void text_put(wfile *w, const char *l)
{
    w_puts(w, l);
    w_putc(w, '\n');
}

/* copy a file of assembler text, noting the names that it defines and uses */
static void copy_text(wfile *w, const char *path)
{
    rfile r;
    char l[104];
    const char *nm;
    u16 n;
    u8 k;
    if (r_open(&r, path)) { err("cannot open a library file"); return; }
    while (text_line(&r, l)) {
        k = marker(l, &nm, &n);
        if (k == 1) add_name(nm, n, 1);
        else if (k == 2) add_name(nm, n, 0);
        text_put(w, l);
    }
    r_close(&r);
}

static u8 pull_blocks(wfile *w, const char *path)
{
    rfile r;
    char l[104];
    const char *nm;
    u16 n;
    u8 k, copied = 0;
    u8 in_block = 0, header = 0, want = 0;
    char head[300];
    u16 hl = 0;
    if (r_open(&r, path)) return 0;
    while (text_line(&r, l)) {
        k = marker(l, &nm, &n);
        if (k == 1) {
            int i;
            if (!header) { hl = 0; want = 0; in_block = 0; }
            header = 1;
            i = find_name(nm, n);
            if (i >= 0 && state[i] == 0) want = 1;
            if (hl + strlen(l) + 2 < sizeof(head)) { memcpy(head + hl, l, strlen(l)); hl += (u16)strlen(l); head[hl++] = '\n'; }
            continue;
        }
        if (header) {
            header = 0;
            in_block = want;
            if (want) {
                u16 p = 0;
                while (p < hl) {
                    u16 q = p;
                    while (q < hl && head[q] != '\n') q++;
                    if (q - p > 6) add_name(head + p + 6, (u16)(q - p - 6), 1);
                    p = (u16)(q + 1);
                }
                for (p = 0; p < hl; p++) w_putc(w, head[p]);
                copied = 1;
            }
        }
        if (in_block) {
            if (k == 2) add_name(nm, n, 0);
            text_put(w, l);
        }
    }
    r_close(&r);
    return copied;
}

/* tmp: the assembler text of the program; dst: start code + that + the library routines */
static void link_library(const char *tmp, const char *dst)
{
    wfile w;
    u8 pass, i, changed;
    if (w_open(&w, dst)) { err("cannot create the output"); return; }
    add_name("_mk_start", 9, 0);
#ifdef CC_HOST
    if (getenv("CC_LIB")) {                 /* (on the PC the library is in a directory of the PC) */
        static char p0[200], p1[200], p2[200];
        strcpy(p0, getenv("CC_LIB")); strcat(p0, "/CLIB.S"); libs[0] = p0;
        strcpy(p1, getenv("CC_LIB")); strcat(p1, "/RT.S"); libs[1] = p1;
        strcpy(p2, getenv("CC_LIB")); strcat(p2, "/START.S");
        copy_text(&w, p2);
    } else
#endif
    copy_text(&w, "/LIB/START.S");
    copy_text(&w, tmp);
    for (pass = 0; pass < 8; pass++) {
        changed = 0;
        for (i = 0; i < NLIB; i++) if (pull_blocks(&w, libs[i])) changed = 1;
        if (!changed) break;
    }
    w_close(&w);
    for (i = 0; i < nnames; i++)
        if (state[i] == 0) { char m[40]; strcpy(m, "undefined: "); strcat(m, names[i]); err(m); }
}

int main(int argc, char **argv)
{
    int i, tail = tail_start(argc, argv);
    const char *src = NULL, *dst = NULL;
    char outname[120];
    u8 libmode = 0;
    for (i = 1; i < tail; i++) {
        if (!strcmp(argv[i], "-o") && i + 1 < tail) dst = argv[++i];
        else if (!strcmp(argv[i], "-L")) libmode = 1;
        else if (argv[i][0] == '-' && argv[i][1] == 'p' && strlen(argv[i]) < 8) strcpy(ptag, argv[i] + 2);
        else if (argv[i][0] == '-') { err("unknown option"); return 2; }
        else src = argv[i];
    }
    if (!src) {
#ifdef CC_HOST
        fputs("usage: cg [-o out.s] file.ir\n", stderr);
#else
        putstr("usage: cg [-o out.s] file.ir\n");
#endif
        return 2;
    }
    if (!dst) {
        u16 n = (u16)strlen(src);
        if (n > 115) n = 115;
        memcpy(outname, src, n);
        outname[n] = 0;
        if (n > 3 && outname[n - 3] == '.' && outname[n - 2] == 'i' && outname[n - 1] == 'r') { outname[n - 2] = 's'; outname[n - 1] = 0; }
        else strcat(outname, ".s");
        dst = outname;
    }
    strcpy(fname, src);
    if (r_open(&in, src)) { err("cannot open the input"); return 2; }
    if (w_open(&out, libmode ? "cg0.tmp" : dst)) { err("cannot create the output"); return 2; }
    prescan();
    r_close(&in);
    r_open(&in, src);
    line = 0;
    toplevel();
    r_close(&in);
    w_close(&out);
    if (libmode && !errors) link_library("cg0.tmp", dst);
    if (libmode) {
#ifdef CC_HOST
        remove("cg0.tmp");
#else
        unlink("cg0.tmp");
#endif
    }
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
