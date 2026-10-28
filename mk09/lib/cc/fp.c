/* fp.c - single precision floating point on 16 bit words (included by clib.c; tests/fp_host_test.c compares it with the float of the PC).
 *
 * A float is IEEE 754 binary32 in a pair (hi, lo): hi = sign (bit 15), exponent (bits 14..7), the top 7 bits of the fraction; lo = the other 16 bits.
 * + - * / and the conversions round to nearest even like the hardware.  A denormal number is taken as 0 and a result below the smallest normal number is 0;
 * an infinity or NaN is passed on as it is but is not computed with exactly.
 * The compiler calls these with addresses: __fadd(d, b, a) is  *d = *a + *b  (see docs/IR.md, "long" has the same rule). */
#ifndef FP_HOST
typedef unsigned u16;
#endif
#ifndef FP_L
#define FP_L
typedef struct { u16 hi, lo; } __L;
#endif

/* ---------------------------------------------------------------- pairs of words as 32 bit numbers */
static void __shr1s(u16 *h, u16 *l, u16 *st)           /* (h:l) >>= 1;  *st gets the bit that falls out */
{
    if (*l & 1) *st = 1;
    *l = (*l >> 1) | (*h << 15);
    *h >>= 1;
}

static void __add32(u16 *h, u16 *l, u16 bh, u16 bl)
{
    u16 lo = *l + bl;
    *h = *h + bh + (lo < *l);
    *l = lo;
}

#ifndef FP_PARSE_ONLY            /* (the compilers that only read a decimal number need only the pack and unpack)  */
static void __shl1(u16 *h, u16 *l)
{
    *h = (*h << 1) | (*l >> 15);
    *l <<= 1;
}

static void __sub32(u16 *h, u16 *l, u16 bh, u16 bl)
{
    u16 lo = *l - bl;
    *h = *h - bh - (*l < bl);
    *l = lo;
}

static int __ge32(u16 ah, u16 al, u16 bh, u16 bl)       /* a >= b (unsigned) */
{
    return ah > bh || (ah == bh && al >= bl);
}

/* 16 x 16 -> 32 bits with 8 bit pieces */
static void __m16(u16 x, u16 y, u16 *hi, u16 *lo)
{
    u16 x0 = x & 255, x1 = x >> 8, y0 = y & 255, y1 = y >> 8;
    u16 p00 = x0 * y0, p01 = x0 * y1, p10 = x1 * y0, p11 = x1 * y1;
    u16 mid = p01 + p10;
    u16 c = mid < p01;
    u16 l = p00 + (mid << 8);
    *hi = p11 + (mid >> 8) + (l < p00) + (c << 8);
    *lo = l;
}

#endif
/* ---------------------------------------------------------------- unpack and pack */
/* the exponent field, the mantissa with the hidden bit as a 24 bit number (mh has 8 bits) */
static void __unpack(__L *a, u16 *sign, int *e, u16 *mh, u16 *ml)
{
    *sign = a->hi & 0x8000;
    *e = (a->hi >> 7) & 255;
    if (*e == 0) { *mh = 0; *ml = 0; return; }
    *mh = (a->hi & 0x7F) | 0x80;
    *ml = a->lo;
}

/* (mh:ml) is a 27 bit number, its top bit at bit 26 (mh in 0x400..0x7FF): the 24 bits of the mantissa, then 3 bits to round with; st: something is lost below */
static void __pack(__L *d, u16 sign, int e, u16 mh, u16 ml, u16 st)
{
    u16 r = ml & 7;
    if (r > 4 || (r == 4 && (st || (ml & 8)))) {
        u16 z = 0;
        __add32(&mh, &ml, 0, 8);
        if (mh & 0x800) {                      /* 1.111...1 rounded up to 10.000 */
            __shr1s(&mh, &ml, &z);
            e++;
        }
    }
    if (e >= 255) { d->hi = sign | 0x7F80; d->lo = 0; return; }
    if (e <= 0) { d->hi = sign; d->lo = 0; return; }
    /* the mantissa = (mh:ml) >> 3 */
    d->lo = (ml >> 3) | (mh << 13);
    d->hi = sign | (e << 7) | ((mh >> 3) & 0x7F);
}

#ifndef FP_PARSE_ONLY
/* ---------------------------------------------------------------- + - */
static void __fadd2(__L *d, __L *b, __L *a, u16 flip)      /* *d = *a + *b, or *a - *b when flip */
{
    u16 sa, sb, mah, mal, mbh, mbl, st = 0, t;
    int ea, eb, df;
    __L x, y;
    x.hi = a->hi; x.lo = a->lo;
    y.hi = b->hi; y.lo = b->lo;
    y.hi ^= flip;
    __unpack(&x, &sa, &ea, &mah, &mal);
    __unpack(&y, &sb, &eb, &mbh, &mbl);
    if (ea == 255) { d->hi = x.hi; d->lo = x.lo; return; }
    if (eb == 255) { d->hi = y.hi; d->lo = y.lo; return; }
    if (ea == 0 && eb == 0) { d->hi = (sa & sb) ? 0x8000 : 0; d->lo = 0; return; }
    if (ea == 0) { d->hi = y.hi; d->lo = y.lo; return; }
    if (eb == 0) { d->hi = x.hi; d->lo = x.lo; return; }
    /* |x| >= |y| */
    if (ea < eb || (ea == eb && !__ge32(mah, mal, mbh, mbl))) {
        t = sa; sa = sb; sb = t;
        t = mah; mah = mbh; mbh = t;
        t = mal; mal = mbl; mbl = t;
        df = ea; ea = eb; eb = df;
    }
    df = ea - eb;
    /* 3 more bits at the bottom */
    __shl1(&mah, &mal); __shl1(&mah, &mal); __shl1(&mah, &mal);
    __shl1(&mbh, &mbl); __shl1(&mbh, &mbl); __shl1(&mbh, &mbl);
    if (df > 27) df = 27;
    while (df-- > 0) __shr1s(&mbh, &mbl, &st);
    if (st) mbl |= 1;                          /* (sticky: in the lowest bit, which is only used to round) */
    st = 0;
    if (sa == sb) {
        __add32(&mah, &mal, mbh, mbl);
        if (mah & 0x800) { __shr1s(&mah, &mal, &st); ea++; }
    } else {
        __sub32(&mah, &mal, mbh, mbl);
        if (!(mah | mal)) { d->hi = 0; d->lo = 0; return; }
        while (!(mah & 0x400)) { __shl1(&mah, &mal); ea--; }
    }
    if (st) mal |= 1;
    __pack(d, sa, ea, mah, mal, 0);
}

void __fadd(__L *d, __L *b, __L *a) { __fadd2(d, b, a, 0); }
void __fsub(__L *d, __L *b, __L *a) { __fadd2(d, b, a, 0x8000); }

/* ---------------------------------------------------------------- * / */
void __fmul(__L *d, __L *b, __L *a)
{
    u16 sa, sb, mah, mal, mbh, mbl, st = 0;
    u16 t0h, t0l, t1h, t1l, t2h, t2l, t3h, t3l, w[4], s, c, k;
    int ea, eb, e;
    __L x, y;
    x.hi = a->hi; x.lo = a->lo;
    y.hi = b->hi; y.lo = b->lo;
    __unpack(&x, &sa, &ea, &mah, &mal);
    __unpack(&y, &sb, &eb, &mbh, &mbl);
    sa ^= sb;
    if (ea == 255 || eb == 255) { d->hi = sa | 0x7F80; d->lo = 0; return; }
    if (ea == 0 || eb == 0) { d->hi = sa; d->lo = 0; return; }
    e = ea + eb - 127;
    /* the 48 bit product = al*bl + ((ah*bl + al*bh) << 16) + ((ah*bh) << 32) */
    __m16(mal, mbl, &t0h, &t0l);
    __m16(mah, mbl, &t1h, &t1l);
    __m16(mal, mbh, &t2h, &t2l);
    __m16(mah, mbh, &t3h, &t3l);
    w[0] = t0l;
    s = t0h + t1l;
    c = s < t0h;
    k = s;
    s += t2l;
    c += s < k;
    w[1] = s;
    s = t1h + t2h + c;
    k = s;
    s += t3l;
    w[3] = t3h + (s < k);
    w[2] = s;
    /* its top bit is 47 or 46 (w[3] is 0): the top 27 bits, and what is below them as the sticky bit */
    if (w[2] & 0x8000) { e++; c = 21; } else c = 20;
    while (c-- > 0) {
        if (w[0] & 1) st = 1;
        w[0] = (w[0] >> 1) | (w[1] << 15);
        w[1] = (w[1] >> 1) | (w[2] << 15);
        w[2] = (w[2] >> 1) | (w[3] << 15);
        w[3] >>= 1;
    }
    __pack(d, sa, e, w[1], w[0], st);
}

void __fdiv(__L *d, __L *b, __L *a)
{
    u16 sa, sb, mah, mal, mbh, mbl, qh = 0, ql = 0, st;
    int ea, eb, e, i;
    __L x, y;
    x.hi = a->hi; x.lo = a->lo;
    y.hi = b->hi; y.lo = b->lo;
    __unpack(&x, &sa, &ea, &mah, &mal);
    __unpack(&y, &sb, &eb, &mbh, &mbl);
    sa ^= sb;
    if (ea == 255) { d->hi = sa | 0x7F80; d->lo = 0; return; }
    if (eb == 0) { d->hi = sa | 0x7F80; d->lo = 0; return; }       /* x / 0 */
    if (eb == 255) { d->hi = sa; d->lo = 0; return; }
    if (ea == 0) { d->hi = sa; d->lo = 0; return; }
    e = ea - eb + 127;
    if (!__ge32(mah, mal, mbh, mbl)) { __shl1(&mah, &mal); e--; }
    for (i = 0; i < 27; i++) {
        __shl1(&qh, &ql);
        if (__ge32(mah, mal, mbh, mbl)) { __sub32(&mah, &mal, mbh, mbl); ql |= 1; }
        __shl1(&mah, &mal);
    }
    st = (mah | mal) != 0;
    __pack(d, sa, e, qh, ql, st);
}

void __fneg(__L *d, __L *a)
{
    u16 hi = a->hi, lo = a->lo;
    d->hi = hi ^ 0x8000;
    d->lo = lo;
}

int __fnz(__L *a) { return (a->hi & 0x7F80) != 0; }

/* the sign of *a - *b  (-1 0 1) */
int __fcmp(__L *b, __L *a)
{
    u16 ah = a->hi, bh = b->hi;
    int neg;
    if (!(ah & 0x7F80) && !(bh & 0x7F80)) return 0;          /* both are 0 */
    if ((ah & 0x8000) != (bh & 0x8000)) return (ah & 0x8000) ? -1 : 1;
    neg = (ah & 0x8000) != 0;
    ah &= 0x7FFF;
    bh &= 0x7FFF;
    if (ah != bh) return (ah < bh) ^ neg ? -1 : 1;
    if (a->lo != b->lo) return (a->lo < b->lo) ^ neg ? -1 : 1;
    return 0;
}

void __finc(__L *p, int delta)
{
    __L one;
    one.hi = delta > 0 ? 0x3F80 : 0xBF80;
    one.lo = 0;
    __fadd(p, &one, p);
}

/* ---------------------------------------------------------------- conversions */
/* the unsigned number (h:l) with the sign: a float */
static void __utof2(__L *d, u16 sign, u16 h, u16 l)
{
    int e, p;
    u16 st = 0;
    if (!(h | l)) { d->hi = 0; d->lo = 0; return; }
    /* the position p of the top bit */
    p = 31;
    while (!(h & 0x8000)) { __shl1(&h, &l); p--; }
    /* now the top bit is bit 31: 27 bits = the top 27 bits, the 5 bits below are a sticky bit */
    {
        u16 mh = h >> 5, ml = (l >> 5) | (h << 11);
        if (l & 31) st = 1;
        e = 127 + p;
        __pack(d, sign, e, mh, ml, st);
    }
}

void __utof(__L *d, u16 v) { __utof2(d, 0, 0, v); }

void __itof(__L *d, int v)
{
    if (v < 0) __utof2(d, 0x8000, 0, -v); else __utof2(d, 0, 0, v);
}

void __ultof(__L *d, __L *a) { __utof2(d, 0, a->hi, a->lo); }

void __ltof(__L *d, __L *a)
{
    u16 h = a->hi, l = a->lo;
    if (h & 0x8000) {
        l = ~l + 1;
        h = ~h + (l == 0);
        __utof2(d, 0x8000, h, l);
    } else __utof2(d, 0, h, l);
}

/* float -> long (towards 0) */
void __ftol(__L *d, __L *a)
{
    u16 sign, mh, ml, h, l, z = 0;
    int e, n;
    __L x;
    x.hi = a->hi; x.lo = a->lo;
    __unpack(&x, &sign, &e, &mh, &ml);
    e -= 127;
    if (e < 0 || mh == 0) { d->hi = 0; d->lo = 0; return; }
    h = mh; l = ml;                           /* 24 bits, the point behind bit 23 */
    if (e >= 23) {
        n = e - 23;
        if (n > 8) n = 8;                     /* (it does not fit in a long) */
        while (n-- > 0) __shl1(&h, &l);
    } else {
        n = 23 - e;
        while (n-- > 0) __shr1s(&h, &l, &z);
    }
    if (sign) { l = ~l + 1; h = ~h + (l == 0); }
    d->hi = h;
    d->lo = l;
}

int __ftoi(__L *a)
{
    __L t;
    __ftol(&t, a);
    return t.lo;
}
#endif
