/* fp_host_test.c - lib/cc/fp.c (the float on 16 bit words) against the float of the PC.   gcc -O0 -w -DFP_HOST -o fp_host_test tests/fp_host_test.c && ./fp_host_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef uint16_t u16;
#include "../lib/cc/fp.c"

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void put(__L *d, float f) { uint32_t u = bits(f); d->hi = u >> 16; d->lo = u & 0xFFFF; }
static uint32_t get(__L *d) { return ((uint32_t)d->hi << 16) | d->lo; }
static int normal(float f) { uint32_t e = (bits(f) >> 23) & 255; return e != 0 && e != 255; }
static int fails, total;
static uint32_t rnd(void) { static uint32_t s = 12345; s = s * 1664525u + 1013904223u; return s ^ (s >> 15); }
static float rf(void)       /* a random float of any size (not too big, so that sums do not overflow) */
{
    uint32_t u = rnd();
    uint32_t e = 60 + (rnd() % 140);
    return flt((u & 0x807FFFFFu) | (e << 23));
}
static void check(const char *op, float a, float b, uint32_t got, float want)
{
    total++;
    if (!normal(want) && want != 0) return;                      /* (inf, nan, denormal: not exact) */
    if (bits(want) != got && !(want == 0 && (got & 0x7FFFFFFF) == 0)) {
        if (fails < 20) printf("FAIL %s %08x %08x: got %08x want %08x\n", op, bits(a), bits(b), got, bits(want));
        fails++;
    }
}
int main(void)
{
    int i;
    for (i = 0; i < 200000; i++) {
        float a = rf(), b = rf();
        __L x, y, d;
        if (i % 4 == 0) b = flt((bits(a) & 0xFF800000u) | (rnd() & 0x7FFFFF) | 0);           /* the same exponent: cancellation */
        if (i % 7 == 0) b = -a;
        if (i % 11 == 0) b = flt(bits(a) ^ 1);
        put(&x, a); put(&y, b);
        volatile float va = a, vb = b;
        __fadd(&d, &y, &x); check("add", a, b, get(&d), va + vb);
        __fsub(&d, &y, &x); check("sub", a, b, get(&d), va - vb);
        __fmul(&d, &y, &x); check("mul", a, b, get(&d), va * vb);
        __fdiv(&d, &y, &x); check("div", a, b, get(&d), va / vb);
        { int c = __fcmp(&y, &x); total++; if (c != (va > vb) - (va < vb)) { if (fails < 20) printf("FAIL cmp %08x %08x: %d\n", bits(a), bits(b), c); fails++; } }
    }
    for (i = 0; i < 100000; i++) {
        uint32_t u = rnd();
        int32_t v = (int32_t)(u >> (rnd() % 32));
        if (rnd() & 1) v = -v;
        __L d, x;
        volatile float w;
        __itof(&d, (int16_t)v); w = (float)(int16_t)v; check("itof", 0, 0, get(&d), w);
        x.hi = (uint32_t)v >> 16; x.lo = v & 0xFFFF;
        __ltof(&d, &x); w = (float)v; check("ltof", 0, 0, get(&d), w);
        __ultof(&d, &x); w = (float)(uint32_t)v; check("ultof", 0, 0, get(&d), w);
        { float f = rf(); if (rnd() & 3) f = flt((bits(f) & 0x807FFFFF) | ((127 + rnd() % 31) << 23));
          put(&x, f); __ftol(&d, &x); total++;
          if (fabsf(f) < 2147483648.0f && (int32_t)get(&d) != (int32_t)f) { if (fails < 20) printf("FAIL ftol %08x: %08x want %08x\n", bits(f), get(&d), (uint32_t)(int32_t)f); fails++; } }
    }
    printf("%d checks, %d failed\n", total, fails);
    return fails != 0;
}
