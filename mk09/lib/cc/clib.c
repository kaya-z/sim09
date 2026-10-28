/* clib.c - the C library of the programs that are compiled on MK09 (compiled by the compiler itself, at the build; see docs/IR.md).
 * The files and the console through the servers, printf, strings, ctype, malloc.  Every function is a block of the assembler text; cc takes the ones
 * that the program uses.  The names that begin with __ are internal (a program may define its own printf or strlen: then these are not used).
 * The headers are in disk/inc (/INC on the disk). */
typedef struct msg { unsigned char src, op; unsigned a, b, c, d; } msg_t;
int mk_call(int dst, msg_t *m);
int mk_callg(int dst, msg_t *m, int gfl);
void mk_exit(int code);
int main(int argc, char **argv);
unsigned strlen(char *s);

/* the float: arithmetic and conversions on pairs of 16 bit words (fp.c), and the text conversions (fpio.c) */
#include "fp.c"
#include "fpio.c"
int (*__fmtfp)(char *out, __L *f, int conv, int prec);          /* the program that has a float sets it (cc1) */

/* ---------------------------------------------------------------- files */
static unsigned char __inh = 255, __outh = 255;      /* the standard files when the shell redirected them (255: the console) */
static char *__argv[34];
static char __obuf[80];
static unsigned char __olen;

static int __rpc(int dst, msg_t *m, int gfl)
{
    int st;
    if (gfl) st = mk_callg(dst, m, gfl); else st = mk_call(dst, m);
    if (st) return -st;
    if (m->op) return -m->op;
    return m->a;
}

static int __path(int op, char *path, int a, msg_t *m)
{
    m->op = op;
    m->a = a;
    m->b = (unsigned)path;
    m->c = strlen(path) + 1;
    return __rpc(3, m, 1);
}

int write(int fd, char *buf, unsigned n)
{
    msg_t m;
    if (fd == 0 && __inh != 255) fd = __inh + 3;
    if (fd == 1 && __outh != 255) fd = __outh + 3;
    m.b = (unsigned)buf;
    m.c = n;
    if (fd < 3) { m.op = 2; return __rpc(1, &m, 1); }
    m.op = 4;
    m.a = fd - 3;
    return __rpc(3, &m, 1);
}

int read(int fd, char *buf, unsigned n)
{
    msg_t m;
    if (fd == 0 && __inh != 255) fd = __inh + 3;
    m.b = (unsigned)buf;
    m.c = n;
    if (fd < 3) { m.op = 1; return __rpc(1, &m, 2); }
    m.op = 3;
    m.a = fd - 3;
    return __rpc(3, &m, 2);
}

int open(char *path, int flags)
{
    msg_t m;
    int r = __path(1, path, flags, &m);
    return r < 0 ? r : r + 3;
}

int close(int fd)
{
    msg_t m;
    int r;
    m.op = 2;
    m.a = fd - 3;
    r = __rpc(3, &m, 0);
    return r < 0 ? r : 0;
}

/* the position in a file: whence 0 = from the start, 1 = from here, 2 = from the end;  returns the new position (the low 16 bits) or -error */
int seek(int fd, int off, int whence)
{
    msg_t m;
    int r;
    m.op = 5;
    m.a = fd - 3;
    m.b = off;
    m.c = off < 0 ? 65535 : 0;
    m.d = whence;
    r = __rpc(3, &m, 0);
    return r < 0 ? r : m.b;
}

int unlink(char *path) { msg_t m; int r = __path(9, path, 0, &m); return r < 0 ? r : 0; }
int mkdir(char *path) { msg_t m; int r = __path(8, path, 0, &m); return r < 0 ? r : 0; }
int chdir(char *path) { msg_t m; int r = __path(11, path, 0, &m); return r < 0 ? r : 0; }

int getcwd(char *buf, unsigned n)
{
    msg_t m;
    int r;
    m.op = 12;
    m.b = (unsigned)buf;
    m.c = n;
    r = __rpc(3, &m, 2);
    return r < 0 ? r : 0;
}

/* ---------------------------------------------------------------- stdio: the console buffer, FILE */
/* stdout is the buffer __obuf (written at a newline); stderr is not buffered; the other files and stdin have a buffer of 64 bytes each */
struct _FILE {
    int fd;                    /* the handle; stdin 0, stdout 1, stderr 2 */
    unsigned char mode;        /* 1 read, 2 write */
    unsigned char eof, err;
    int unget;                 /* a character pushed back, or -1 */
    unsigned char pos, len;    /* reading: the next character and how many there are;  writing: how many are waiting */
    char buf[64];
};
typedef struct _FILE FILE;
#define NFILE 6
static FILE __files[NFILE];
FILE *stdin = __files;
FILE *stdout = __files + 1;
FILE *stderr = __files + 2;

static void __initfiles(void)
{
    if (__files[0].mode) return;
    __files[0].mode = 1; __files[0].fd = 0; __files[0].unget = -1;
    __files[1].mode = 2; __files[1].fd = 1; __files[1].unget = -1;
    __files[2].mode = 2; __files[2].fd = 2; __files[2].unget = -1;
}

int fflush_out(void)
{
    unsigned char n = __olen;
    __olen = 0;
    if (n) write(1, __obuf, n);
    return 0;
}

static int __flushf(FILE *f)
{
    unsigned char n = f->len;
    f->len = 0;
    if (n && write(f->fd, f->buf, n) != n) { f->err = 1; return -1; }
    return 0;
}

int fflush(FILE *f)
{
    if (!f || f->fd == 1) return fflush_out();
    if (f->mode == 2) return __flushf(f);
    return 0;
}

int putchar(int c)
{
    __obuf[__olen++] = c;
    if (c == 10 || __olen >= 80) fflush_out();
    return c;
}

int fputc(int c, FILE *f)
{
    char ch;
    __initfiles();
    if (f->fd == 1) return putchar(c);
    if (f->fd == 2) { ch = c; write(2, &ch, 1); return c & 255; }
    if (f->mode != 2) return -1;
    f->buf[f->len++] = c;
    if (f->len >= 64 && __flushf(f)) return -1;
    return c & 255;
}

int fgetc(FILE *f)
{
    int c, n;
    __initfiles();
    if (f->fd == 0) fflush_out();
    if (f->unget >= 0) { c = f->unget; f->unget = -1; return c; }
    if (f->mode != 1) return -1;
    if (f->pos >= f->len) {
        n = read(f->fd, f->buf, 64);
        if (n <= 0) { f->eof = 1; return -1; }
        f->pos = 0;
        f->len = n;
    }
    return f->buf[f->pos++] & 255;
}

int ungetc(int c, FILE *f)
{
    if (c < 0 || f->unget >= 0) return -1;
    f->unget = c;
    f->eof = 0;
    return c;
}

int putc(int c, FILE *f) { return fputc(c, f); }
int getc(FILE *f) { return fgetc(f); }
int getchar(void) { return fgetc(stdin); }
int feof(FILE *f) { return f->eof; }
int ferror(FILE *f) { return f->err; }

int fputs(char *s, FILE *f)
{
    while (*s) fputc(*s++, f);
    return 0;
}

int puts(char *s)
{
    while (*s) putchar(*s++);
    putchar(10);
    return 0;
}

char *fgets(char *buf, int n, FILE *f)
{
    int c, i = 0;
    while (i < n - 1) {
        c = fgetc(f);
        if (c < 0) break;
        buf[i++] = c;
        if (c == 10) break;
    }
    if (!i) return 0;
    buf[i] = 0;
    return buf;
}

unsigned fread(char *p, unsigned size, unsigned n, FILE *f)
{
    unsigned i, k, total = size * n;
    int c;
    for (i = 0; i < total; i++) {
        c = fgetc(f);
        if (c < 0) break;
        p[i] = c;
    }
    return size ? i / size : 0;
}

unsigned fwrite(char *p, unsigned size, unsigned n, FILE *f)
{
    unsigned i, total = size * n;
    for (i = 0; i < total; i++) if (fputc(p[i], f) < 0) break;
    return size ? i / size : 0;
}

FILE *fopen(char *path, char *mode)
{
    FILE *f;
    int fl, fd, i;
    __initfiles();
    if (mode[0] == 'r') fl = mode[1] == '+' ? 3 : 1;
    else if (mode[0] == 'w') fl = 2 | 4 | 8 | (mode[1] == '+' ? 1 : 0);
    else if (mode[0] == 'a') fl = 2 | 4 | 16;
    else return 0;
    for (i = 3; i < NFILE; i++) if (!__files[i].mode) break;
    if (i == NFILE) return 0;
    fd = open(path, fl);
    if (fd < 0) return 0;
    f = __files + i;
    f->fd = fd;
    f->mode = (mode[0] == 'r' && mode[1] != '+') ? 1 : 2;
    f->eof = f->err = f->pos = f->len = 0;
    f->unget = -1;
    return f;
}

int fclose(FILE *f)
{
    int r = 0;
    if (f->mode == 2) r = fflush(f);
    if (f->fd >= 3) close(f->fd);
    f->mode = 0;
    return r;
}

int fseek(FILE *f, int off, int whence)
{
    int r;
    if (f->mode == 2) __flushf(f);
    else if (whence == 1) off -= f->len - f->pos;      /* (the characters that are read already but not used) */
    f->pos = f->len = 0;
    f->unget = -1;
    f->eof = 0;
    r = seek(f->fd, off, whence);
    return r < 0 ? -1 : 0;
}

int ftell(FILE *f)
{
    int r;
    if (f->mode == 2) __flushf(f);
    r = seek(f->fd, 0, 1);
    if (f->mode == 1) r -= f->len - f->pos;
    if (f->unget >= 0) r--;
    return r;
}

int remove(char *path) { return unlink(path); }

/* a line from the standard input without the newline; returns the length, or -1 at the end of the input */
int readline(char *buf, int n)
{
    int c, i = 0;
    for (;;) {
        c = getchar();
        if (c < 0) { if (!i) return -1; break; }
        if (c == 10) break;
        if (i < n - 1) buf[i++] = c;
    }
    buf[i] = 0;
    return i;
}

void exit(int code)
{
    int i;
    fflush_out();
    for (i = 3; i < NFILE; i++) if (__files[i].mode == 2) fflush(__files + i);      /* (only the files that are open) */
    mk_exit(code);
}

/* printf: %d %u %x %c %s %%, a width, - and 0 */
static void (*__out)(int c);
static char *__sp;

static void __putsp(int c) { *__sp++ = c; }

static void __putnum(unsigned v, unsigned base, int width, int zero, int left, int neg)
{
    char t[8];
    int n = 0, i;
    if (neg && zero && !left) { __out('-'); width--; neg = 0; }       /* (the sign comes before the zeros) */
    do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) t[n++] = '-';
    if (!left) for (i = n; i < width; i++) __out(zero ? '0' : ' ');
    i = n;
    while (n) __out(t[--n]);
    if (left) for (; i < width; i++) __out(' ');
}

static void __putlong(unsigned hi, unsigned lo, unsigned base, int width, int zero, int left, int neg)
{
    char t[12];
    int n = 0, i;
    unsigned r, qh, ql;
    if (neg && zero && !left) { __out('-'); width--; neg = 0; }
    while (hi | lo) {                  /* (hi:lo) / base, base <= 16 */
        qh = hi / base;
        r = hi % base;
        ql = 0;
        for (i = 0; i < 16; i++) {
            r = (r << 1) | (lo >> 15);
            lo <<= 1;
            ql <<= 1;
            if (r >= base) { r -= base; ql |= 1; }
        }
        hi = qh;
        lo = ql;
        t[n++] = "0123456789abcdef"[r];
    }
    if (!n) t[n++] = '0';
    if (neg) t[n++] = '-';
    if (!left) for (i = n; i < width; i++) __out(zero ? '0' : ' ');
    i = n;
    while (n) __out(t[--n]);
    if (left) for (; i < width; i++) __out(' ');
}

static void __fmt(char *fmt, int *ap)
{
    char c;
    while (c = *fmt++) {
        int width = 0, zero = 0, left = 0, n, len, lg = 0, prec = -1;
        unsigned hi, lo;
        char *s;
        char tb[48];
        __L fv;
        if (c != '%') { __out(c); continue; }
        for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else break; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == '.') {
            fmt++;
            prec = 0;
            while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + *fmt++ - '0';
        }
        if (*fmt == 'l') { fmt++; lg = 1; }
        c = *fmt++;
        if (c == 'f' || c == 'e' || c == 'g' || c == 'E' || c == 'G') {          /* a float: 2 words */
            fv.hi = *ap++;
            fv.lo = *ap++;
            if (prec > 20) prec = 20;
            if (__fmtfp) len = __fmtfp(tb, &fv, c, prec);
            else { tb[0] = '?'; tb[1] = 0; len = 1; }
            s = tb;
            if (zero && !left && tb[0] == '-') { __out('-'); s++; len--; width--; }
            if (!left) for (n = len; n < width; n++) __out(zero ? '0' : ' ');
            while (*s) __out(*s++);
            if (left) for (n = len; n < width; n++) __out(' ');
        } else if (lg && (c == 'd' || c == 'u' || c == 'x')) {          /* a long: 2 words, the high one first */
            hi = *ap++;
            lo = *ap++;
            n = 0;
            if (c == 'd' && (hi & 0x8000)) { lo = ~lo + 1; hi = ~hi + (lo == 0); n = 1; }
            __putlong(hi, lo, c == 'x' ? 16 : 10, width, zero, left, n);
        } else if (c == 'd') {
            n = *ap++;
            if (n < 0) __putnum(-n, 10, width, zero, left, 1); else __putnum(n, 10, width, zero, left, 0);
        } else if (c == 'u') __putnum(*ap++, 10, width, zero, left, 0);
        else if (c == 'x') __putnum(*ap++, 16, width, zero, left, 0);
        else if (c == 'c') __out(*ap++);
        else if (c == 's') {
            s = (char *)*ap++;
            len = strlen(s);
            if (prec >= 0 && len > prec) len = prec;
            if (!left) for (n = len; n < width; n++) __out(' ');
            for (n = 0; n < len; n++) __out(s[n]);
            if (left) for (n = len; n < width; n++) __out(' ');
        } else __out(c);
    }
}

int printf(char *fmt, ...)
{
    __out = putchar;
    __fmt(fmt, (int *)&fmt + 1);
    return 0;
}

static FILE *__of;

static void __putf(int c) { fputc(c, __of); }

int fprintf(FILE *f, char *fmt, ...)
{
    __of = f;
    __out = __putf;
    __fmt(fmt, (int *)&fmt + 1);
    return 0;
}

int sprintf(char *buf, char *fmt, ...)
{
    __out = __putsp;
    __sp = buf;
    __fmt(fmt, (int *)&fmt + 1);
    *__sp = 0;
    return __sp - buf;
}

/* ---------------------------------------------------------------- strings and memory */
unsigned strlen(char *s)
{
    char *p = s;
    while (*p) p++;
    return p - s;
}

char *strcpy(char *d, char *s)
{
    char *r = d;
    while (*d++ = *s++) ;
    return r;
}

char *strncpy(char *d, char *s, unsigned n)
{
    char *r = d;
    while (n && (*d = *s)) { d++; s++; n--; }
    while (n--) *d++ = 0;
    return r;
}

char *strcat(char *d, char *s)
{
    char *r = d;
    while (*d) d++;
    while (*d++ = *s++) ;
    return r;
}

int strcmp(char *a, char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (*a & 255) - (*b & 255);
}

int strncmp(char *a, char *b, unsigned n)
{
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (*a & 255) - (*b & 255) : 0;
}

char *strchr(char *s, int c)
{
    for (;; s++) {
        if (*s == (char)c) return s;
        if (!*s) return 0;
    }
}

char *strrchr(char *s, int c)
{
    char *r = 0;
    for (;; s++) {
        if (*s == (char)c) r = s;
        if (!*s) return r;
    }
}

char *strstr(char *h, char *n)
{
    unsigned l = strlen(n);
    for (; *h; h++) if (!strncmp(h, n, l)) return h;
    return 0;
}

char *memcpy(char *d, char *s, unsigned n)
{
    char *r = d;
    while (n--) *d++ = *s++;
    return r;
}

char *memmove(char *d, char *s, unsigned n)
{
    char *r = d;
    if (d < s) while (n--) *d++ = *s++;
    else { d += n; s += n; while (n--) *--d = *--s; }
    return r;
}

char *memset(char *d, int c, unsigned n)
{
    char *r = d;
    while (n--) *d++ = c;
    return r;
}

int memcmp(char *a, char *b, unsigned n)
{
    while (n--) {
        if (*a != *b) return (*a & 255) - (*b & 255);
        a++; b++;
    }
    return 0;
}

/* ---------------------------------------------------------------- ctype */
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
int toupper(int c) { return islower(c) ? c - 32 : c; }
int tolower(int c) { return isupper(c) ? c + 32 : c; }

/* ---------------------------------------------------------------- stdlib */
int abs(int v) { return v < 0 ? -v : v; }

int atoi(char *s)
{
    int n = 0, neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (isdigit(*s)) n = n * 10 + *s++ - '0';
    return neg ? -n : n;
}

static unsigned __seed = 1;

void srand(unsigned s) { __seed = s; }

int rand(void)
{
    __seed = __seed * 25173 + 13849;
    return (__seed >> 1) & 32767;
}

/* a heap of 3000 bytes in the bss: blocks with a 2 byte header (the size of the block, the lowest bit = in use) */
#define HEAPSIZE 3000
static char __heap[HEAPSIZE];
static int __heapinit;

char *malloc(unsigned n)
{
    unsigned *h;
    unsigned s;
    if (!__heapinit) { *(unsigned *)__heap = HEAPSIZE - 2; __heapinit = 1; }
    n = (n + 1) & 0xFFFE;
    if (n < 2) n = 2;
    h = (unsigned *)__heap;
    while ((char *)h < __heap + HEAPSIZE) {
        s = *h & 0xFFFE;
        if (!(*h & 1) && s >= n) {
            if (s >= n + 4) {
                *(unsigned *)((char *)h + 2 + n) = s - n - 2;
                *h = n | 1;
            } else *h = s | 1;
            return (char *)h + 2;
        }
        h = (unsigned *)((char *)h + 2 + s);
    }
    return 0;
}

void free(char *p)
{
    unsigned *h, *nx;
    if (!p) return;
    h = (unsigned *)(p - 2);
    *h &= 0xFFFE;
    /* join the free blocks that follow each other */
    h = (unsigned *)__heap;
    while ((char *)h < __heap + HEAPSIZE) {
        unsigned s = *h & 0xFFFE;
        nx = (unsigned *)((char *)h + 2 + s);
        if (!(*h & 1) && (char *)nx < __heap + HEAPSIZE && !(*nx & 1)) *h = s + 2 + (*nx & 0xFFFE);
        else h = nx;
    }
}

char *calloc(unsigned n, unsigned size)
{
    char *p = malloc(n * size);
    if (p) memset(p, 0, n * size);
    return p;
}

void qsort(char *base, unsigned n, unsigned size, int (*cmp)(char *, char *))
{
    unsigned i, j, k;
    char t;
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && cmp(base + (j - 1) * size, base + j * size) > 0; j--)
            for (k = 0; k < size; k++) {
                t = base[(j - 1) * size + k];
                base[(j - 1) * size + k] = base[j * size + k];
                base[j * size + k] = t;
            }
}

/* ---------------------------------------------------------------- long: 4 bytes, the high word first.  The compiler calls these with the addresses of the
 * operands (docs/IR.md): __ladd(d, b, a) is  *d = *a + *b;  a function that returns a long leaves it in __lret */
__L __lret;

void __itol(__L *d, int v) { d->lo = v; d->hi = v < 0 ? 65535 : 0; }
void __utol(__L *d, unsigned v) { d->lo = v; d->hi = 0; }

void __ladd(__L *d, __L *b, __L *a)
{
    unsigned lo = a->lo + b->lo;
    unsigned hi = a->hi + b->hi + (lo < a->lo);
    d->hi = hi;
    d->lo = lo;
}

void __lsub(__L *d, __L *b, __L *a)
{
    unsigned lo = a->lo - b->lo;
    unsigned hi = a->hi - b->hi - (a->lo < b->lo);
    d->hi = hi;
    d->lo = lo;
}

void __land(__L *d, __L *b, __L *a) { d->hi = a->hi & b->hi; d->lo = a->lo & b->lo; }
void __lor(__L *d, __L *b, __L *a) { d->hi = a->hi | b->hi; d->lo = a->lo | b->lo; }
void __lxor(__L *d, __L *b, __L *a) { d->hi = a->hi ^ b->hi; d->lo = a->lo ^ b->lo; }

void __lneg(__L *d, __L *a)
{
    unsigned lo = ~a->lo + 1;
    unsigned hi = ~a->hi + (lo == 0);
    d->hi = hi;
    d->lo = lo;
}

void __lcom(__L *d, __L *a) { d->hi = ~a->hi; d->lo = ~a->lo; }
int __lnz(__L *a) { return (a->hi | a->lo) != 0; }

int __lcmp(__L *b, __L *a)
{
    if (a->hi != b->hi) return (int)a->hi < (int)b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

int __lucmp(__L *b, __L *a)
{
    if (a->hi != b->hi) return a->hi < b->hi ? -1 : 1;
    if (a->lo != b->lo) return a->lo < b->lo ? -1 : 1;
    return 0;
}

void __linc(__L *p, int delta)
{
    if (delta > 0) { p->lo++; if (!p->lo) p->hi++; }
    else { if (!p->lo) p->hi--; p->lo--; }
}

void __lshl(__L *d, int n, __L *a)
{
    unsigned hi = a->hi, lo = a->lo;
    if (n > 32) n = 32;
    while (n-- > 0) { hi = (hi << 1) | (lo >> 15); lo <<= 1; }
    d->hi = hi;
    d->lo = lo;
}

void __lushr(__L *d, int n, __L *a)
{
    unsigned hi = a->hi, lo = a->lo;
    if (n > 32) n = 32;
    while (n-- > 0) { lo = (lo >> 1) | (hi << 15); hi >>= 1; }
    d->hi = hi;
    d->lo = lo;
}

void __lshr(__L *d, int n, __L *a)
{
    unsigned hi = a->hi, lo = a->lo;
    if (n > 32) n = 32;
    while (n-- > 0) { lo = (lo >> 1) | (hi << 15); hi = (hi >> 1) | (hi & 0x8000); }
    d->hi = hi;
    d->lo = lo;
}

/* 16 x 16 -> 32 bits with 8 bit pieces */
static void __mul16(unsigned x, unsigned y, unsigned *hi, unsigned *lo)
{
    unsigned x0 = x & 255, x1 = x >> 8, y0 = y & 255, y1 = y >> 8;
    unsigned p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
    unsigned mid = p01 + p10;
    unsigned c = mid < p01;
    unsigned l = p00 + (mid << 8);
    *hi = p11 + (mid >> 8) + (l < p00) + (c << 8);
    *lo = l;
}

void __lmul(__L *d, __L *b, __L *a)
{
    unsigned h, l;
    unsigned ah = a->hi, al = a->lo, bh = b->hi, bl = b->lo;
    __mul16(al, bl, &h, &l);
    h += ah * bl + al * bh;
    d->hi = h;
    d->lo = l;
}

/* unsigned division: q = n / d, r = n % d (a division by 0 gives 0 and 0) */
static void __udm(__L *q, __L *r, __L *n, __L *d)
{
    unsigned qh = n->hi, ql = n->lo, rh = 0, rl = 0, dh = d->hi, dl = d->lo, top;
    int i;
    if (!(dh | dl)) { q->hi = 0; q->lo = 0; r->hi = 0; r->lo = 0; return; }
    for (i = 0; i < 32; i++) {
        top = rh & 0x8000;
        rh = (rh << 1) | (rl >> 15);
        rl = (rl << 1) | (qh >> 15);
        qh = (qh << 1) | (ql >> 15);
        ql <<= 1;
        if (top || rh > dh || (rh == dh && rl >= dl)) {
            top = rl < dl;
            rl -= dl;
            rh = rh - dh - top;
            ql |= 1;
        }
    }
    q->hi = qh; q->lo = ql;
    r->hi = rh; r->lo = rl;
}

void __ludiv(__L *d, __L *b, __L *a)
{
    __L q, r;
    __udm(&q, &r, a, b);
    d->hi = q.hi; d->lo = q.lo;
}

void __lumod(__L *d, __L *b, __L *a)
{
    __L q, r;
    __udm(&q, &r, a, b);
    d->hi = r.hi; d->lo = r.lo;
}

void __ldiv(__L *d, __L *b, __L *a)
{
    __L x, y, q, r;
    int neg = 0;
    x.hi = a->hi; x.lo = a->lo;
    y.hi = b->hi; y.lo = b->lo;
    if ((int)x.hi < 0) { __lneg(&x, &x); neg = 1; }
    if ((int)y.hi < 0) { __lneg(&y, &y); neg ^= 1; }
    __udm(&q, &r, &x, &y);
    if (neg) __lneg(&q, &q);
    d->hi = q.hi; d->lo = q.lo;
}

void __lmod(__L *d, __L *b, __L *a)
{
    __L x, y, q, r;
    int neg = 0;
    x.hi = a->hi; x.lo = a->lo;
    y.hi = b->hi; y.lo = b->lo;
    if ((int)x.hi < 0) { __lneg(&x, &x); neg = 1; }
    if ((int)y.hi < 0) __lneg(&y, &y);
    __udm(&q, &r, &x, &y);
    if (neg) __lneg(&r, &r);
    d->hi = r.hi; d->lo = r.lo;
}

long atol(char *s)
{
    long n = 0;
    int neg = 0;
    while (isspace(*s)) s++;
    if (*s == '-') { neg = 1; s++; } else if (*s == '+') s++;
    while (isdigit(*s)) n = n * 10 + (*s++ - '0');
    return neg ? -n : n;
}

long labs(long v) { return v < 0 ? -v : v; }

/* ---------------------------------------------------------------- float (a double is a float) */
float atof(char *s)
{
    float r;
    __strtof(s, (__L *)&r, 0);
    return r;
}

float strtod(char *s, char **end)
{
    float r;
    __strtof(s, (__L *)&r, end);
    return r;
}

float fabs(float x) { if (x < 0) return -x; return x; }

float sqrt(float x)               /* Newton's method */
{
    float g, h;
    int i;
    if (x <= 0) return 0;
    if (x > 1) g = x / 2; else g = 1;
    for (i = 0; i < 24; i++) {
        h = (g + x / g) / 2;
        if (h == g) break;
        g = h;
    }
    return g;
}

float floor(float x)
{
    long n = (long)x;
    float f = (float)n;
    if (f > x) f = f - 1;
    return f;
}

float ceil(float x)
{
    long n = (long)x;
    float f = (float)n;
    if (f < x) f = f + 1;
    return f;
}

/* ---------------------------------------------------------------- the start of a program (called by start.s with the start-up block of PM) */
void mk_start(char *blk)
{
    char *p = blk + 4;
    int i, argc;
    __inh = blk[1];
    __outh = blk[2];
    argc = blk[3];
    if (argc > 32) argc = 32;
    for (i = 0; i < argc; i++) {
        __argv[i] = p;
        while (*p) p++;
        p++;
    }
    __argv[argc] = 0;
    exit(main(argc, __argv));
}
