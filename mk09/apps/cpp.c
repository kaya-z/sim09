/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * cpp - the C preprocessor of the C compiler of MK09 (docs/IR.md)
 *
 *   cpp [-Dname[=value]] [-Idir] [-o out] file.c
 *
 * Comments are removed, lines joined at a backslash, "#include" (a name in quotes is looked for in the directory of the file, then in /INC and
 * the -I directories; <name> in /INC and the -I directories), "#define" (a name, or a name with parameters; no # and ## operators), "#undef",
 * "#if" "#ifdef" "#ifndef" "#elif" "#else" "#endif" (the expression has integers, defined, ! ~ - + * / % << >> < > <= >= == != & ^ | && || ?: and
 * parentheses; a name that is not a macro is 0), "#error" (stops), "#pragma" (ignored).  The output has the same lines as the input (a line that
 * is taken away leaves an empty one), and a line  # N "file"  when a file is entered or left.
 */
#include "ccio.h"

/* the float constants (1.5, 2e-3f, .5) are turned into  @ and the 8 hex digits of the IEEE bits  (cc1 reads that) */
#define FP_HOST
#define FP_PARSE_ONLY
#include "../lib/cc/fp.c"
#include "../lib/cc/fpio.c"

#ifdef CC_HOST
#define fputs_err(m) fputs(m, stderr)
#else
#define fputs_err(m) putstr(m)
#endif

#define LINEMAX 300
#define NMAC    90
#define MPOOL   3000
#define MAXD    5           /* depth of macro expansion */
#define MAXARG  8
#define MAXINC  6
#define NIDIR   3

typedef struct macro {
    char *name;
    char *params;           /* "a,b" or NULL for a macro without parentheses */
    char *body;
} macro_t;

static macro_t macs[NMAC];
static u8 nmac;
static char mpool[MPOOL];
static u16 mlen;

static wfile out;
static const char *idirs[NIDIR];
static u8 nidirs;
static u8 errors;
static const char *curname;
static u16 curline;
static u8 incdepth;

static void err(const char *msg)
{
    errors++;
    w_flush(&out);
#ifdef CC_HOST
    fprintf(stderr, "%s:%u: %s\n", curname, curline, msg);
#else
    printf("%s:%u: %s\n", curname, curline, msg);
#endif
}

static int isidc(int c, u8 first)
{
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') return 1;
    return !first && c >= '0' && c <= '9';
}

static char *pool_str(const char *s, u16 n)
{
    char *p;
    if (mlen + n + 1 > MPOOL) { err("too many macros"); return mpool; }
    p = mpool + mlen;
    memcpy(p, s, n);
    p[n] = 0;
    mlen += n + 1;
    return p;
}

static macro_t *find_macro(const char *s, u16 n)
{
    u8 i;
    for (i = 0; i < nmac; i++)
        if (macs[i].name && !strncmp(macs[i].name, s, n) && macs[i].name[n] == 0) return &macs[i];
    return NULL;
}

/* ---------------------------------------------------------------- macro expansion */
static char *active[MAXD + 2];
static u8 nactive;
static char subst[MAXD + 1][LINEMAX];
static char argpool[MAXD + 1][LINEMAX * 2];

static u8 is_active(const char *name)
{
    u8 i;
    for (i = 0; i < nactive; i++) if (active[i] == name) return 1;
    return 0;
}

static void expand(const char *s, char *o, u16 max, u8 depth);

/* the length of the floating constant at s (digits with a point or an exponent, and an f or l), or 0 if it is not one */
static u16 float_len(const char *s)
{
    u16 i = 0, digits = 0, j;
    u8 dot = 0, ex = 0;
    while (s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    if (s[i] == '.') {
        dot = 1;
        i++;
        while (s[i] >= '0' && s[i] <= '9') { i++; digits++; }
    }
    if (!digits) return 0;
    if (s[i] == 'e' || s[i] == 'E') {
        j = i + 1;
        if (s[j] == '+' || s[j] == '-') j++;
        if (s[j] >= '0' && s[j] <= '9') {
            while (s[j] >= '0' && s[j] <= '9') j++;
            i = j;
            ex = 1;
        }
    }
    if (!dot && !ex) return 0;
    if (s[i] == 'f' || s[i] == 'F' || s[i] == 'l' || s[i] == 'L') i++;
    return i;
}

/* the arguments of a call: s is behind '(', the arguments are put into argpool[depth] (expanded), their addresses into args; returns the text behind ')' */
static const char *get_args(const char *s, char **args, u8 *nargs, u8 depth)
{
    u8 n = 0;
    u16 used = 0;
    u8 level = 0;
    char raw[LINEMAX];
    u16 r = 0;
    for (;;) {
        char c = *s;
        if (c == 0) { err("missing ) in a macro call"); break; }
        if (c == '"' || c == '\'') {
            char q = c;
            do {
                if (r < LINEMAX - 2) raw[r++] = *s;
                if (*s == '\\' && s[1]) { s++; if (r < LINEMAX - 2) raw[r++] = *s; }
                s++;
            } while (*s && *s != q);
            if (*s) { if (r < LINEMAX - 2) raw[r++] = *s; s++; }
            continue;
        }
        if (c == '(') level++;
        if (c == ')') {
            if (level == 0) { s++; break; }
            level--;
        }
        if (c == ',' && level == 0) {
            raw[r] = 0;
            if (n < MAXARG) {
                args[n] = argpool[depth] + used;
                expand(raw, args[n], LINEMAX * 2 - used, depth + 1);
                used += (u16)strlen(args[n]) + 1;
                n++;
            }
            r = 0;
            s++;
            continue;
        }
        if (r < LINEMAX - 2) raw[r++] = c;
        s++;
    }
    raw[r] = 0;
    if (n < MAXARG && (n > 0 || r > 0)) {
        args[n] = argpool[depth] + used;
        expand(raw, args[n], LINEMAX * 2 - used, depth + 1);
        n++;
    }
    *nargs = n;
    return s;
}

static u8 param_index(const char *params, const char *id, u16 n)
{
    u8 i = 0;
    const char *p = params;
    for (;;) {
        const char *e = p;
        while (*e && *e != ',') e++;
        if ((u16)(e - p) == n && !strncmp(p, id, n)) return i;
        if (!*e) return 255;
        p = e + 1;
        i++;
    }
}

static void expand(const char *s, char *o, u16 max, u8 depth)
{
    u16 n = 0;
#define PUT(ch) do { if (n < max - 1) o[n++] = (ch); } while (0)
    while (*s) {
        char c = *s;
        if (c == '"' || c == '\'') {
            char q = c;
            PUT(*s++);
            while (*s && *s != q) {
                if (*s == '\\' && s[1]) PUT(*s++);
                PUT(*s++);
            }
            if (*s) PUT(*s++);
        } else if (c == '@') {                  /* (a float constant that is converted already) */
            PUT(*s++);
            while (isidc(*s, 0)) PUT(*s++);
        } else if ((c >= '0' && c <= '9') || (c == '.' && s[1] >= '0' && s[1] <= '9')) {
            u16 fl = (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) ? 0 : float_len(s);
            if (fl) {
                char tmp[40], hx[9];
                __L v;
                u8 k;
                const char *h = "0123456789ABCDEF";
                u16 cp = fl;
                if (cp > 39) cp = 39;
                memcpy(tmp, s, cp);
                tmp[cp] = 0;
                __strtof(tmp, &v, 0);
                hx[0] = h[v.hi >> 12]; hx[1] = h[(v.hi >> 8) & 15]; hx[2] = h[(v.hi >> 4) & 15]; hx[3] = h[v.hi & 15];
                hx[4] = h[v.lo >> 12]; hx[5] = h[(v.lo >> 8) & 15]; hx[6] = h[(v.lo >> 4) & 15]; hx[7] = h[v.lo & 15];
                PUT('@');
                for (k = 0; k < 8; k++) PUT(hx[k]);
                s += fl;
            } else {
                while (isidc(*s, 0) || *s == '.') PUT(*s++);
            }
        } else if (isidc(c, 1)) {
            const char *b = s;
            u16 len;
            macro_t *m;
            while (isidc(*s, 0)) s++;
            len = (u16)(s - b);
            m = find_macro(b, len);
            if (m && !is_active(m->name) && depth < MAXD) {
                if (!m->params) {
                    char *res = subst[depth];
                    active[nactive++] = m->name;
                    expand(m->body, res, LINEMAX, depth + 1);
                    nactive--;
                    for (res = subst[depth]; *res; res++) PUT(*res);
                } else {
                    const char *t = s;
                    while (*t == ' ' || *t == '\t') t++;
                    if (*t != '(') {
                        for (; b < s; b++) PUT(*b);
                    } else {
                        char *args[MAXARG];
                        u8 na = 0, i;
                        char body[LINEMAX];
                        const char *p;
                        u16 bn = 0;
                        s = get_args(t + 1, args, &na, depth);
                        /* substitute the parameters in the body */
                        p = m->body;
                        while (*p) {
                            if (isidc(*p, 1)) {
                                const char *q = p;
                                u8 k;
                                while (isidc(*p, 0)) p++;
                                k = param_index(m->params, q, (u16)(p - q));
                                if (k != 255 && k < na) {
                                    const char *a = args[k];
                                    while (*a) { if (bn < LINEMAX - 1) body[bn++] = *a; a++; }
                                } else {
                                    for (; q < p; q++) if (bn < LINEMAX - 1) body[bn++] = *q;
                                }
                            } else if (*p >= '0' && *p <= '9') {
                                while (isidc(*p, 0)) { if (bn < LINEMAX - 1) body[bn++] = *p; p++; }
                            } else {
                                if (bn < LINEMAX - 1) body[bn++] = *p;
                                p++;
                            }
                        }
                        body[bn] = 0;
                        (void)i;
                        active[nactive++] = m->name;
                        expand(body, subst[depth], LINEMAX, depth + 1);
                        nactive--;
                        for (p = subst[depth]; *p; p++) PUT(*p);
                    }
                }
            } else {
                for (; b < s; b++) PUT(*b);
            }
        } else {
            PUT(*s++);
        }
    }
    o[n] = 0;
#undef PUT
}

/* ---------------------------------------------------------------- #if expressions */
static const char *ep;
static int cexpr(u8 lv);

static void eskip(void) { while (*ep == ' ' || *ep == '\t') ep++; }

static int eprimary(void)
{
    int v = 0;
    eskip();
    if (*ep == '(') {
        ep++;
        v = cexpr(0);
        eskip();
        if (*ep == ')') ep++; else err("missing ) in #if");
    } else if (*ep == '!') { ep++; v = !eprimary(); }
    else if (*ep == '~') { ep++; v = ~eprimary(); }
    else if (*ep == '-') { ep++; v = -eprimary(); }
    else if (*ep == '+') { ep++; v = eprimary(); }
    else if (*ep == '\'') {
        ep++;
        v = (u8)*ep;
        if (*ep == '\\') {
            ep++;
            v = *ep == 'n' ? 10 : *ep == 't' ? 9 : *ep == 'r' ? 13 : *ep == '0' ? 0 : (u8)*ep;
        }
        if (*ep) ep++;
        if (*ep == '\'') ep++;
    } else if (*ep >= '0' && *ep <= '9') {
        if (ep[0] == '0' && (ep[1] == 'x' || ep[1] == 'X')) {
            ep += 2;
            while ((*ep >= '0' && *ep <= '9') || (*ep >= 'a' && *ep <= 'f') || (*ep >= 'A' && *ep <= 'F')) {
                v = v * 16 + (*ep <= '9' ? *ep - '0' : (*ep | 32) - 'a' + 10);
                ep++;
            }
        } else if (ep[0] == '0') {
            while (*ep >= '0' && *ep <= '7') v = v * 8 + (*ep++ - '0');
        } else {
            while (*ep >= '0' && *ep <= '9') v = v * 10 + (*ep++ - '0');
        }
        while (*ep == 'u' || *ep == 'U' || *ep == 'l' || *ep == 'L') ep++;
    } else if (isidc(*ep, 1)) {
        while (isidc(*ep, 0)) ep++;       /* a name that is not a macro */
        v = 0;
    } else {
        err("bad #if expression");
        if (*ep) ep++;
    }
    return v;
}

/* levels: 0 ?:  1 ||  2 &&  3 |  4 ^  5 &  6 == !=  7 < > <= >=  8 << >>  9 + -  10 * / % */
static int cexpr(u8 lv)
{
    int a, b;
    if (lv == 11) return eprimary();
    if (lv == 0) {
        a = cexpr(1);
        eskip();
        if (*ep == '?') {
            int t, f;
            ep++;
            t = cexpr(0);
            eskip();
            if (*ep == ':') ep++; else err("missing : in #if");
            f = cexpr(0);
            return a ? t : f;
        }
        return a;
    }
    a = cexpr(lv + 1);
    for (;;) {
        char c, d;
        eskip();
        c = ep[0];
        d = ep[1];
        if (lv == 1 && c == '|' && d == '|') { ep += 2; b = cexpr(2); a = (a || b); }
        else if (lv == 2 && c == '&' && d == '&') { ep += 2; b = cexpr(3); a = (a && b); }
        else if (lv == 3 && c == '|' && d != '|') { ep++; b = cexpr(4); a = a | b; }
        else if (lv == 4 && c == '^') { ep++; b = cexpr(5); a = a ^ b; }
        else if (lv == 5 && c == '&' && d != '&') { ep++; b = cexpr(6); a = a & b; }
        else if (lv == 6 && c == '=' && d == '=') { ep += 2; b = cexpr(7); a = a == b; }
        else if (lv == 6 && c == '!' && d == '=') { ep += 2; b = cexpr(7); a = a != b; }
        else if (lv == 7 && c == '<' && d == '=') { ep += 2; b = cexpr(8); a = a <= b; }
        else if (lv == 7 && c == '>' && d == '=') { ep += 2; b = cexpr(8); a = a >= b; }
        else if (lv == 7 && c == '<' && d != '<') { ep++; b = cexpr(8); a = a < b; }
        else if (lv == 7 && c == '>' && d != '>') { ep++; b = cexpr(8); a = a > b; }
        else if (lv == 8 && c == '<' && d == '<') { ep += 2; b = cexpr(9); a = a << b; }
        else if (lv == 8 && c == '>' && d == '>') { ep += 2; b = cexpr(9); a = a >> b; }
        else if (lv == 9 && (c == '+' || c == '-')) { ep++; b = cexpr(10); a = c == '+' ? a + b : a - b; }
        else if (lv == 10 && (c == '*' || c == '/' || c == '%')) {
            ep++;
            b = cexpr(11);
            if (c == '*') a = a * b;
            else if (b == 0) { err("division by zero in #if"); a = 0; }
            else a = c == '/' ? a / b : a % b;
        } else break;
    }
    return a;
}

/* the text of a #if line: defined X and defined(X) become 1 or 0, the macros are expanded, then it is evaluated */
static int if_value(const char *text)
{
    char t[LINEMAX], e[LINEMAX];
    u16 n = 0;
    const char *s = text;
    while (*s) {
        if (isidc(*s, 1)) {
            const char *b = s;
            while (isidc(*s, 0)) s++;
            if ((u16)(s - b) == 7 && !strncmp(b, "defined", 7)) {
                const char *id;
                u8 par = 0;
                while (*s == ' ' || *s == '\t') s++;
                if (*s == '(') { par = 1; s++; while (*s == ' ' || *s == '\t') s++; }
                id = s;
                while (isidc(*s, 0)) s++;
                if (n < LINEMAX - 2) t[n++] = find_macro(id, (u16)(s - id)) ? '1' : '0';
                if (par) { while (*s == ' ' || *s == '\t') s++; if (*s == ')') s++; }
            } else {
                for (; b < s; b++) if (n < LINEMAX - 2) t[n++] = *b;
            }
        } else if (n < LINEMAX - 2) t[n++] = *s++;
        else s++;
    }
    t[n] = 0;
    expand(t, e, LINEMAX, 0);
    ep = e;
    return cexpr(0);
}

/* ---------------------------------------------------------------- directives */
typedef struct cond {
    u8 active;      /* the lines of this level are taken */
    u8 taken;       /* one of its branches was taken */
    u8 parent;      /* the enclosing level is active */
} cond_t;
#define NCOND 12

static void define(const char *s)
{
    const char *b;
    u16 n;
    char *name, *params = NULL, *body;
    macro_t *m;
    while (*s == ' ' || *s == '\t') s++;
    b = s;
    if (!isidc(*s, 1)) { err("bad #define"); return; }
    while (isidc(*s, 0)) s++;
    n = (u16)(s - b);
    name = pool_str(b, n);
    if (*s == '(') {
        const char *p = ++s;
        while (*s && *s != ')') s++;
        {
            char tmp[80];
            u16 k = 0;
            const char *q;
            for (q = p; q < s && k < 78; q++) if (*q != ' ' && *q != '\t') tmp[k++] = *q;
            tmp[k] = 0;
            params = pool_str(tmp, k);
        }
        if (*s == ')') s++;
    }
    while (*s == ' ' || *s == '\t') s++;
    body = pool_str(s, (u16)strlen(s));
    /* trailing blanks */
    {
        u16 l = (u16)strlen(body);
        while (l && (body[l - 1] == ' ' || body[l - 1] == '\t')) body[--l] = 0;
    }
    m = find_macro(name, n);
    if (!m) {
        if (nmac >= NMAC) { err("too many macros"); return; }
        m = &macs[nmac++];
    }
    m->name = name;
    m->params = params;
    m->body = body;
}

static void undef(const char *s)
{
    const char *b;
    macro_t *m;
    while (*s == ' ' || *s == '\t') s++;
    b = s;
    while (isidc(*s, 0)) s++;
    m = find_macro(b, (u16)(s - b));
    if (m) m->name = NULL;
}

static void marker(u16 line, const char *file)
{
    w_puts(&out, "# ");
    w_num(&out, (int)line);
    w_puts(&out, " \"");
    w_puts(&out, file);
    w_puts(&out, "\"\n");
}

static void process(const char *path, const char *from_dir);

static void dir_of(const char *path, char *d)
{
    const char *sl = strrchr(path, '/');
    u16 n = sl ? (u16)(sl - path + 1) : 0;
    memcpy(d, path, n);
    d[n] = 0;
}

static int r_open_probe(const char *path)
{
    rfile t;
    if (r_open(&t, path)) return -1;
    r_close(&t);
    return 0;
}

static void include(const char *s, const char *curdir)
{
    char name[80], path[130];
    char open_c, close_c;
    u8 i, found = 0;
    u16 n = 0;
    while (*s == ' ' || *s == '\t') s++;
    open_c = *s++;
    close_c = open_c == '<' ? '>' : '"';
    if (open_c != '<' && open_c != '"') { err("bad #include"); return; }
    while (*s && *s != close_c && n < 78) name[n++] = *s++;
    name[n] = 0;
    if (open_c == '"') {
        strcpy(path, curdir);
        strcat(path, name);
        if (!r_open_probe(path)) found = 1;
    }
    for (i = 0; !found && i <= nidirs; i++) {
        strcpy(path, i == 0 ? "/INC/" : idirs[i - 1]);
        if (i > 0) { u16 l = (u16)strlen(path); if (l && path[l - 1] != '/') strcat(path, "/"); }
        strcat(path, name);
        if (!r_open_probe(path)) found = 1;
    }
    if (!found) { err("cannot find the include file"); return; }
    if (incdepth >= MAXINC) { err("includes nested too deep"); return; }
    {
        const char *sn = curname;
        u16 sl = curline;
        char keep[130];
        strcpy(keep, path);
        process(keep, curdir);
        curname = sn;
        curline = sl;
        marker((u16)(curline + 1), curname);
    }
}


/* ---------------------------------------------------------------- reading lines */
static int comment_open;       /* inside a comment that goes on in the next line */
static int cprev;              /* the last character in a comment */

/* the next logical line (backslash-newline joined, comments replaced by a blank); returns the number of physical lines, 0 at the end of the file */
static u8 read_line(rfile *r, char *l)
{
    u16 n = 0;
    u8 lines = 0;
    int c;
    u8 q = 0;                  /* inside a string or character constant: the quote */
    for (;;) {
        c = r_getc(r);
        if (c < 0) { if (n == 0 && lines == 0) return 0; break; }
        if (c == '\r') continue;
        if (comment_open) {
            if (c == '\n') lines++;
            if (cprev == '*' && c == '/') { comment_open = 0; if (n < LINEMAX - 1) l[n++] = ' '; cprev = 0; }
            else cprev = c;
            continue;
        }
        if (c == '\n') { lines++; break; }
        if (c == '\\') {
            int d = r_getc(r);
            if (d == '\n') { lines++; continue; }
            if (n < LINEMAX - 1) l[n++] = '\\';
            if (d >= 0 && n < LINEMAX - 1) l[n++] = (char)d;
            if (d < 0) break;
            continue;
        }
        if (q) {
            if (n < LINEMAX - 1) l[n++] = (char)c;
            if (c == q) q = 0;
            continue;
        }
        if (c == '"' || c == '\'') { q = (u8)c; if (n < LINEMAX - 1) l[n++] = (char)c; continue; }
        if (c == '/') {
            int d = r_getc(r);
            if (d == '*') { comment_open = 1; cprev = 0; continue; }
            if (d == '/') {
                while ((c = r_getc(r)) >= 0 && c != '\n') ;
                lines++;
                break;
            }
            if (n < LINEMAX - 1) l[n++] = '/';
            if (d == '\n') { lines++; break; }
            if (d < 0) break;
            c = d;
            if (c == '"' || c == '\'') { q = (u8)c; }
            if (n < LINEMAX - 1) l[n++] = (char)c;
            continue;
        }
        if (n < LINEMAX - 1) l[n++] = (char)c;
    }
    l[n] = 0;
    return lines ? lines : 1;
}

static void process(const char *path, const char *from_dir)
{
    rfile in;
    char line[LINEMAX], dir[120];
    cond_t cs[NCOND];
    u8 nc = 0;
    u8 skip = 0;
    u8 k;
    (void)from_dir;
    if (r_open(&in, path)) { err("cannot open the file"); return; }
    incdepth++;
    dir_of(path, dir);
    curname = path;
    curline = 1;
    comment_open = 0;
    marker(1, path);
    for (;;) {
        char *s;
        u8 phys = read_line(&in, line);
        if (!phys) break;
        s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (*s == '#') {
            char *d;
            char *dend;
            u16 dl;
            s++;
            while (*s == ' ' || *s == '\t') s++;
            d = s;
            while (isidc(*s, 0)) s++;
            dend = s;
            dl = (u16)(dend - d);
            while (*s == ' ' || *s == '\t') s++;
            if (dl == 2 && !strncmp(d, "if", 2)) {
                if (nc < NCOND) {
                    cs[nc].parent = !skip;
                    cs[nc].active = !skip && if_value(s) != 0;
                    cs[nc].taken = cs[nc].active;
                    nc++;
                    skip = !cs[nc - 1].active;
                } else err("conditions nested too deep");
            } else if ((dl == 5 && !strncmp(d, "ifdef", 5)) || (dl == 6 && !strncmp(d, "ifndef", 6))) {
                if (nc < NCOND) {
                    char *b = s;
                    u8 def;
                    while (isidc(*s, 0)) s++;
                    def = find_macro(b, (u16)(s - b)) != NULL;
                    if (dl == 6) def = !def;
                    cs[nc].parent = !skip;
                    cs[nc].active = !skip && def;
                    cs[nc].taken = cs[nc].active;
                    nc++;
                    skip = !cs[nc - 1].active;
                } else err("conditions nested too deep");
            } else if (dl == 4 && !strncmp(d, "elif", 4)) {
                if (!nc) err("#elif without #if");
                else {
                    cond_t *c = &cs[nc - 1];
                    if (c->taken || !c->parent) c->active = 0;
                    else { c->active = if_value(s) != 0; c->taken = c->active; }
                    skip = !c->active;
                }
            } else if (dl == 4 && !strncmp(d, "else", 4)) {
                if (!nc) err("#else without #if");
                else {
                    cond_t *c = &cs[nc - 1];
                    c->active = c->parent && !c->taken;
                    c->taken = 1;
                    skip = !c->active;
                }
            } else if (dl == 5 && !strncmp(d, "endif", 5)) {
                if (!nc) err("#endif without #if");
                else { nc--; skip = nc ? !cs[nc - 1].active : 0; }
            } else if (!skip) {
                if (dl == 6 && !strncmp(d, "define", 6)) define(s);
                else if (dl == 5 && !strncmp(d, "undef", 5)) undef(s);
                else if (dl == 7 && !strncmp(d, "include", 7)) {
                    char exp[LINEMAX];
                    /* #include MACRO is not supported: the name is in quotes or in <> */
                    (void)exp;
                    include(s, dir);
                    curline = (u16)(curline + phys);
                    for (k = 0; k < phys; k++) w_putc(&out, '\n');
                    continue;
                }
                else if (dl == 5 && !strncmp(d, "error", 5)) { err(s); }
                else if (dl == 6 && !strncmp(d, "pragma", 6)) { }
                else if (dl == 4 && !strncmp(d, "line", 4)) { }
                else if (dl == 0) { }
                else err("unknown directive");
            }
            for (k = 0; k < phys; k++) w_putc(&out, '\n');
        } else if (skip) {
            for (k = 0; k < phys; k++) w_putc(&out, '\n');
        } else {
            char exp[LINEMAX];
            expand(line, exp, LINEMAX, 0);
            w_puts(&out, exp);
            for (k = 0; k < phys; k++) w_putc(&out, '\n');
        }
        curline = (u16)(curline + phys);
    }
    if (nc) err("#if without #endif");
    r_close(&in);
    incdepth--;
}

int main(int argc, char **argv)
{
    int i, tail = tail_start(argc, argv);
    const char *src = NULL, *dst = NULL;
    char outname[120];
    for (i = 1; i < tail; i++) {
        char *a = argv[i];
        if (a[0] == '-' && a[1] == 'D') {
            char def[100];
            char *eq = strchr(a + 2, '=');
            if (eq) { *eq = ' '; strcpy(def, a + 2); }
            else { strcpy(def, a + 2); strcat(def, " 1"); }
            define(def);
        } else if (a[0] == '-' && a[1] == 'I') {
            if (nidirs < NIDIR) idirs[nidirs++] = a + 2;
        } else if (!strcmp(a, "-o") && i + 1 < tail) dst = argv[++i];
        else if (a[0] == '-') { fputs_err("cpp: unknown option\n"); return 2; }
        else src = a;
    }
    if (!src) { fputs_err("usage: cpp [-Dname[=value]] [-Idir] [-o out] file.c\n"); return 2; }
    if (!dst) {
        u16 n = (u16)strlen(src);
        if (n > 115) n = 115;
        memcpy(outname, src, n);
        outname[n] = 0;
        if (n > 2 && outname[n - 2] == '.') outname[n - 1] = 'i'; else strcat(outname, ".i");
        dst = outname;
    }
    if (w_open(&out, dst)) { fputs_err("cpp: cannot create the output file\n"); return 2; }
    process(src, "");
    w_close(&out);
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
