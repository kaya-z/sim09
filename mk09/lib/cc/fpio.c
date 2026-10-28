/* fpio.c - float <-> decimal text on 16 bit words (included by clib.c after fp.c, and by apps/cpp.c, which turns the float constants into the 32 bits).
 * The conversions are exact (big numbers made of words), so the digits are those of the PC: the value of the float is printed with correct rounding
 * (a tie goes to the even digit), and a decimal text becomes the nearest float (a tie goes to the even mantissa). */
#ifndef FP_L
#define FP_L
typedef struct { u16 hi, lo; } __L;
#endif

#define BW 20                                  /* the words of a big number (little endian) */

static void __bmul(u16 *b, u16 n, u16 m, u16 add)         /* b = b * m + add */
{
    u16 i, hi, lo, carry = add;
    for (i = 0; i < n; i++) {
        u16 x = b[i], y = m, x0 = x & 255, x1 = x >> 8, y0 = y & 255, y1 = y >> 8;
        u16 p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
        u16 mid = p01 + p10, c = mid < p01;
        lo = p00 + (mid << 8);
        hi = p11 + (mid >> 8) + (lo < p00) + (c << 8);
        lo += carry;
        hi += lo < carry;
        b[i] = lo;
        carry = hi;
    }
}

static u16 __bdiv(u16 *b, u16 n, u16 d)                   /* b = b / d (d < 32768), returns the remainder */
{
    u16 r = 0, q, w, bit;
    int i, k;
    for (i = n - 1; i >= 0; i--) {
        w = b[i];
        q = 0;
        for (k = 0; k < 16; k++) {
            bit = w >> 15;
            w <<= 1;
            r = (r << 1) | bit;
            q <<= 1;
            if (r >= d) { r -= d; q |= 1; }
        }
        b[i] = q;
    }
    return r;
}

static void __bshl(u16 *b, u16 n, u16 bits)               /* b <<= bits */
{
    int i;
    u16 words = bits >> 4, s = bits & 15;
    if (words) {
        for (i = n - 1; i >= 0; i--) b[i] = i >= words ? b[i - words] : 0;
    }
    if (s) {
        for (i = n - 1; i >= 0; i--) b[i] = (b[i] << s) | (i > 0 ? b[i - 1] >> (16 - s) : 0);
    }
}

static int __bzero(u16 *b, u16 n)
{
    u16 i;
    for (i = 0; i < n; i++) if (b[i]) return 0;
    return 1;
}

#ifndef FP_PARSE_ONLY
/* the digits of a float (positive, finite):  mode 0: the digits of the integer part (at least one) and then n digits behind the point, *xp = the number of digits of
 * the integer part;  mode 1: n significant digits, *xp = the decimal exponent X of d.ddd * 10^X.  Rounded correctly.  Returns the number of digits in g. */
static int __fdig(__L *f, char *g, int mode, int n, int *xp)
{
    u16 T[BW], I[8], F[10];
    u16 sign, mh, ml;
    int e, nint, i, count, want, fz = 0, d, up, E, k, len, s0, rest;
    __L x;
    x.hi = f->hi; x.lo = f->lo;
    __unpack(&x, &sign, &e, &mh, &ml);
    if (!e) {                                   /* 0 */
        for (i = 0; i < n + 1; i++) g[i] = '0';
        g[n + 1] = 0;
        *xp = mode ? 0 : 1;
        return mode ? n : n + 1;
    }
    for (i = 0; i < 8; i++) I[i] = 0;
    for (i = 0; i < 10; i++) F[i] = 0;
    E = e - 150;                               /* value = M * 2^E, M = mh:ml (24 bits) */
    if (E >= 0) {
        I[0] = ml; I[1] = mh;
        __bshl(I, 8, E);
    } else {
        k = -E;
        for (i = 0; i < BW; i++) T[i] = 0;
        T[0] = ml; T[1] = mh;
        __bshl(T, BW, 160 - k);                /* the point is behind 160 bits (10 words) */
        for (i = 0; i < 10; i++) F[i] = T[i];
        I[0] = T[10]; I[1] = T[11];
    }
    /* the digits of the integer part */
    {
        char t[48];
        nint = 0;
        while (!__bzero(I, 8)) t[nint++] = '0' + __bdiv(I, 8, 10);
        if (!nint) t[nint++] = '0';
        for (i = 0; i < nint; i++) g[i] = t[nint - 1 - i];
    }
    count = nint;
    if (mode == 0) want = nint + n;
    else if (g[0] != '0') want = n;
    else want = 1000;                          /* (until the first digit that is not 0) */
    /* the digits of the fraction, and one more to round with */
    while (count < want + 1 && count < 90) {
        u16 carry = 0, w;
        for (i = 0; i < 10; i++) {              /* F * 10 */
            u16 x0 = F[i] & 255, x1 = F[i] >> 8, p00 = x0 * 10, p10 = x1 * 10, lo, hi;
            lo = p00 + (p10 << 8);
            hi = (p10 >> 8) + (lo < p00);
            lo += carry;
            hi += lo < carry;
            F[i] = lo;
            carry = hi;
        }
        w = carry;
        g[count++] = '0' + w;
        if (mode == 1 && want == 1000 && w != 0) { fz = count - 1; want = fz + n; }
    }
    rest = !__bzero(F, 10);
    for (i = want + 1; i < nint; i++) if (g[i] != '0') rest = 1;          /* (the cut is in the integer part) */
    d = g[want] - '0';
    up = d > 5 || (d == 5 && (rest || ((g[want - 1] - '0') & 1)));
    s0 = mode ? fz : 0;
    len = mode ? n : want;
    for (i = 0; i < len; i++) g[i] = g[s0 + i];
    *xp = mode ? (fz == 0 ? nint - 1 : -fz) : nint;
    if (up) {
        i = len - 1;
        while (i >= 0) {
            if (g[i] == '9') { g[i] = '0'; i--; }
            else { g[i]++; break; }
        }
        if (i < 0) {                            /* 999 -> 1000 */
            for (i = len; i > 0; i--) g[i] = g[i - 1];
            g[0] = '1';
            *xp += 1;
            if (!mode) len++;
        }
    }
    g[len] = 0;
    return len;
}

/* the text of a float: conv 'f', 'e' or 'g' (and 'E' 'G'), prec digits;  returns the length */
int __fmtf(char *out, __L *f, int conv, int prec)
{
    char g[100];
    int o = 0, len, x, i, neg = (f->hi & 0x8000) != 0, upper = conv < 'a', strip = 0;
    __L a;
    a.hi = f->hi & 0x7FFF; a.lo = f->lo;
    if (neg) out[o++] = '-';
    if (((a.hi >> 7) & 255) == 255) {          /* inf, nan */
        out[o++] = upper ? 'I' : 'i'; out[o++] = upper ? 'N' : 'n'; out[o++] = upper ? 'F' : 'f';
        out[o] = 0;
        return o;
    }
    if (conv >= 'a') conv -= 32; else conv = conv;
    if (prec < 0) prec = 6;
    if (conv == 'G') {
        if (prec == 0) prec = 1;
        len = __fdig(&a, g, 1, prec, &x);
        if (x < -4 || x >= prec) { conv = 'E'; prec--; strip = 1; }
        else { conv = 'F'; prec = prec - 1 - x; strip = 1; }
    }
    if (conv == 'F') {
        len = __fdig(&a, g, 0, prec, &x);
        for (i = 0; i < x; i++) out[o++] = g[i];
        if (prec) {
            int last = o;
            out[o++] = '.';
            for (i = 0; i < prec; i++) out[o++] = g[x + i];
            if (strip) {
                while (o > last + 1 && out[o - 1] == '0') o--;
                if (out[o - 1] == '.') o--;
            }
        }
    } else {
        len = __fdig(&a, g, 1, prec + 1, &x);
        out[o++] = g[0];
        if (prec) {
            int last = o;
            out[o++] = '.';
            for (i = 1; i <= prec; i++) out[o++] = g[i];
            if (strip) {
                while (o > last + 1 && out[o - 1] == '0') o--;
                if (out[o - 1] == '.') o--;
            }
        }
        out[o++] = upper ? 'E' : 'e';
        if (x < 0) { out[o++] = '-'; x = -x; } else out[o++] = '+';
        if (x >= 100) out[o++] = '0' + x / 100;
        out[o++] = '0' + (x / 10) % 10;
        out[o++] = '0' + x % 10;
    }
    out[o] = 0;
    return o;
}

#endif

/* decimal text -> float:  [+-] digits [. digits] [e [+-] digits] ;  *endp = the end of the text.  The result is the nearest float. */
static void __strtof(char *s, __L *d, char **endp)
{
    u16 N[BW], st = 0, sign = 0;
    int nd = 0, e10 = 0, seen = 0, ex, i, bits, k, e2;
    char c;
    char *p = s;
    for (i = 0; i < BW; i++) N[i] = 0;
    while (*p == ' ' || *p == '\t') p++;
    if (*p == '-') { sign = 0x8000; p++; } else if (*p == '+') p++;
    for (;; p++) {                              /* the digits (at most 28 count) */
        c = *p;
        if (c >= '0' && c <= '9') {
            seen = 1;
            if (nd < 28) { if (nd || c != '0') { __bmul(N, 8, 10, c - '0'); nd++; } }
            else { if (c != '0') st = 1; e10++; }
        } else break;
    }
    if (*p == '.') {
        p++;
        for (;; p++) {
            c = *p;
            if (c >= '0' && c <= '9') {
                seen = 1;
                if (nd < 28) { if (nd || c != '0') { __bmul(N, 8, 10, c - '0'); nd++; } e10--; }
                else if (c != '0') st = 1;
            } else break;
        }
    }
    if (seen && (*p == 'e' || *p == 'E')) {
        char *q = p + 1;
        int es = 1;
        if (*q == '-') { es = -1; q++; } else if (*q == '+') q++;
        if (*q >= '0' && *q <= '9') {
            ex = 0;
            while (*q >= '0' && *q <= '9') { if (ex < 1000) ex = ex * 10 + *q - '0'; q++; }
            e10 += es * ex;
            p = q;
        }
    }
    if (endp) *endp = seen ? p : s;
    d->hi = sign; d->lo = 0;
    if (__bzero(N, BW)) return;
    /* N * 10^e10 */
    if (e10 > 40) { d->hi = sign | 0x7F80; return; }
    if (e10 < -90) return;
    if (e10 >= 0) {
        for (i = 0; i < e10; i++) __bmul(N, BW, 10, 0);
        e2 = 0;
    } else {
        /* N / 10^k: shift N left first so that the quotient has at least 28 bits */
        int bl = 0;
        k = -e10;
        for (i = BW - 1; i >= 0; i--) if (N[i]) { u16 w = N[i]; bl = i * 16; while (w) { bl++; w >>= 1; } break; }
        bits = 28 + (k * 10 + 2) / 3 + 4 - bl;  /* (10^k < 2^(3.33k+1)) */
        if (bits < 0) bits = 0;
        if (bl + bits > 16 * BW - 8) bits = 16 * BW - 8 - bl;
        __bshl(N, BW, bits);
        for (i = 0; i < k; i++) if (__bdiv(N, BW, 10)) st = 1;
        e2 = -bits;
    }
    /* the top 27 bits of N and the sticky bit */
    {
        int bl = 0;
        u16 h, l;
        for (i = BW - 1; i >= 0; i--) if (N[i]) { u16 w = N[i]; bl = i * 16; while (w) { bl++; w >>= 1; } break; }
        if (!bl) return;
        /* shift N so that its top bit is bit 26 of the 27 bit number (N[1]:N[0]) */
        k = bl - 27;
        if (k > 0) {
            for (i = 0; i < k; i++) {
                u16 j, carry = 0;
                if (N[0] & 1) st = 1;
                for (j = BW; j > 0; j--) { u16 w = N[j - 1]; N[j - 1] = (w >> 1) | carry; carry = w << 15; }
            }
        } else if (k < 0) __bshl(N, BW, -k);
        h = N[1]; l = N[0];
        __pack(d, sign, bl - 1 + e2 + 127, h, l, st);
    }
}
