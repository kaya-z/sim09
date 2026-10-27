/* clib.c - the C library of the programs that are compiled on MK09 (compiled by cc itself): the files and the console through the servers,
 * printf, strings.  Every function is a block of the assembler text; the driver cc takes the ones that the program uses. */
typedef struct msg { unsigned char src, op; unsigned a, b, c, d; } msg_t;
int mk_call(int dst, msg_t *m);
int mk_callg(int dst, msg_t *m, int gfl);
void mk_exit(int code);
int main(int argc, char **argv);

static unsigned char in_h = 255, out_h = 255;
static char *argv_[18];
static char obuf[80];
static unsigned char olen;

int write(int fd, char *buf, unsigned n)
{
    msg_t m;
    int st;
    if (fd == 0 && in_h != 255) fd = in_h + 3;
    if (fd == 1 && out_h != 255) fd = out_h + 3;
    m.b = (unsigned)buf;
    m.c = n;
    if (fd < 3) { m.op = 2; st = mk_callg(1, &m, 1); }
    else { m.op = 4; m.a = fd - 3; st = mk_callg(3, &m, 1); }
    if (st) return -st;
    if (m.op) return -m.op;
    return m.a;
}

int read(int fd, char *buf, unsigned n)
{
    msg_t m;
    int st;
    if (fd == 0 && in_h != 255) fd = in_h + 3;
    m.b = (unsigned)buf;
    m.c = n;
    if (fd < 3) { m.op = 1; st = mk_callg(1, &m, 2); }
    else { m.op = 3; m.a = fd - 3; st = mk_callg(3, &m, 2); }
    if (st) return -st;
    if (m.op) return -m.op;
    return m.a;
}

int open(char *path, int flags)
{
    msg_t m;
    int st;
    m.op = 1;
    m.a = flags;
    m.b = (unsigned)path;
    m.c = strlen(path) + 1;
    st = mk_callg(3, &m, 1);
    if (st) return -st;
    if (m.op) return -m.op;
    return m.a + 3;
}

int close(int fd)
{
    msg_t m;
    int st;
    m.op = 2;
    m.a = fd - 3;
    st = mk_call(3, &m);
    if (st) return -st;
    if (m.op) return -m.op;
    return 0;
}

int fflush(void)
{
    unsigned char n = olen;
    olen = 0;
    if (n) write(1, obuf, n);
    return 0;
}

int putchar(int c)
{
    obuf[olen++] = c;
    if (c == 10 || olen >= 80) fflush();
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

void exit(int code)
{
    fflush();
    mk_exit(code);
}

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

int strcmp(char *a, char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (*a & 255) - (*b & 255);
}

char *memcpy(char *d, char *s, unsigned n)
{
    char *r = d;
    while (n--) *d++ = *s++;
    return r;
}

char *memset(char *d, int c, unsigned n)
{
    char *r = d;
    while (n--) *d++ = c;
    return r;
}

int abs(int v) { return v < 0 ? -v : v; }

static void putnum(unsigned v, unsigned base, int width, int zero, int left, int neg)
{
    char t[8];
    int n = 0, i;
    do { t[n++] = "0123456789abcdef"[v % base]; v /= base; } while (v);
    if (neg) t[n++] = '-';
    if (!left) for (i = n; i < width; i++) putchar(zero ? '0' : ' ');
    i = n;
    while (n) putchar(t[--n]);
    if (left) for (; i < width; i++) putchar(' ');
}

int printf(char *fmt, ...)
{
    int *ap = (int *)&fmt + 1;
    char c;
    while (c = *fmt++) {
        int width = 0, zero = 0, left = 0, n, len;
        char *s;
        if (c != '%') { putchar(c); continue; }
        for (;; fmt++) { if (*fmt == '-') left = 1; else if (*fmt == '0') zero = 1; else break; }
        while (*fmt >= '0' && *fmt <= '9') width = width * 10 + *fmt++ - '0';
        if (*fmt == 'l') fmt++;
        c = *fmt++;
        if (c == 'd') {
            n = *ap++;
            if (n < 0) putnum(-n, 10, width, zero, left, 1); else putnum(n, 10, width, zero, left, 0);
        } else if (c == 'u') putnum(*ap++, 10, width, zero, left, 0);
        else if (c == 'x') putnum(*ap++, 16, width, zero, left, 0);
        else if (c == 'c') putchar(*ap++);
        else if (c == 's') {
            s = (char *)*ap++;
            len = strlen(s);
            if (!left) for (n = len; n < width; n++) putchar(' ');
            while (*s) putchar(*s++);
            if (left) for (n = len; n < width; n++) putchar(' ');
        } else putchar(c);
    }
    return 0;
}

void mk_start(char *blk)
{
    char *p = blk + 4;
    int i, argc;
    in_h = blk[1];
    out_h = blk[2];
    argc = blk[3];
    if (argc > 16) argc = 16;
    for (i = 0; i < argc; i++) {
        argv_[i] = p;
        while (*p) p++;
        p++;
    }
    argv_[argc] = 0;
    exit(main(argc, argv_));
}
