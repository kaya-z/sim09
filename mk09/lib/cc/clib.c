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

/* ---------------------------------------------------------------- stdio */
int fflush(void)
{
    unsigned char n = __olen;
    __olen = 0;
    if (n) write(1, __obuf, n);
    return 0;
}

int putchar(int c)
{
    __obuf[__olen++] = c;
    if (c == 10 || __olen >= 80) fflush();
    return c;
}

int puts(char *s)
{
    while (*s) putchar(*s++);
    putchar(10);
    return 0;
}

int getchar(void)
{
    char c;
    fflush();
    if (read(0, &c, 1) != 1) return -1;
    return c & 255;
}

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
    fflush();
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
    do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
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
        int width = 0, zero = 0, left = 0, n, len;
        char *s;
        if (c != '%') { __out(c); continue; }
        for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else break; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == 'l') fmt++;
        c = *fmt++;
        if (c == 'd') {
            n = *ap++;
            if (n < 0) __putnum(-n, 10, width, zero, left, 1); else __putnum(n, 10, width, zero, left, 0);
        } else if (c == 'u') __putnum(*ap++, 10, width, zero, left, 0);
        else if (c == 'x') __putnum(*ap++, 16, width, zero, left, 0);
        else if (c == 'c') __out(*ap++);
        else if (c == 's') {
            s = (char *)*ap++;
            len = strlen(s);
            if (!left) for (n = len; n < width; n++) __out(' ');
            while (*s) __out(*s++);
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
