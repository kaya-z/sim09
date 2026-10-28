/* fpio_host_test.c - lib/cc/fpio.c (float <-> text on 16 bit words) against the printf and strtof of the PC.
 *   gcc -O0 -w -DFP_HOST -o fpio_host_test tests/fpio_host_test.c && ./fpio_host_test */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
typedef uint16_t u16;
#include "../lib/cc/fp.c"
#include "../lib/cc/fpio.c"

static uint32_t bits(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }
static float flt(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }
static void put(__L *d, float f) { uint32_t u = bits(f); d->hi = u >> 16; d->lo = u & 0xFFFF; }
static uint32_t rnd(void) { static uint32_t s = 777; s = s * 1664525u + 1013904223u; return s ^ (s >> 15); }
static int fails, total;

static void tf(float f, char conv, int prec)
{
    char want[200], got[200], fmt[16];
    __L x;
    put(&x, f);
    sprintf(fmt, "%%.%d%c", prec, conv);
    sprintf(want, fmt, (double)f);
    __fmtf(got, &x, conv, prec);
    total++;
    if (strcmp(got, want)) { if (fails < 25) printf("FAIL %%.%d%c %08x: got %s want %s\n", prec, conv, bits(f), got, want); fails++; }
}

int main(void)
{
    int i, p;
    float special[] = { 0.0f, 1.0f, 0.5f, 1.5f, 2.5f, 0.125f, 100.0f, 999.9999f, 0.0001f, 123456.789f, 1e10f, 3.4e38f, 1.2e-30f, 0.1f, 0.3f, 9.5f, 99.5f, 0.05f, 1e-5f };
    for (i = 0; i < (int)(sizeof special / sizeof special[0]); i++)
        for (p = 0; p <= 9; p++) { tf(special[i], 'f', p); tf(special[i], 'e', p); tf(special[i], 'g', p); tf(-special[i], 'f', p); }
    for (i = 0; i < 40000; i++) {
        float f = flt((rnd() & 0x807FFFFF) | ((60 + rnd() % 140) << 23));
        if (i % 3 == 0) f = flt((rnd() & 0x807FFFFF) | ((120 + rnd() % 20) << 23));
        tf(f, 'f', 6); tf(f, 'e', 6); tf(f, 'g', 6); tf(f, 'f', rnd() % 8); tf(f, 'e', rnd() % 12); tf(f, 'g', 1 + rnd() % 9);
    }
    /* text -> float */
    {
        const char *t[] = { "0", "1", "1.5", "0.1", "3.14159265", "1e10", "1.5e-5", "123456789", "0.000001", "16777217", "340282346638528859811704183484516925440", "2.5e-3", "100", ".5", "5.", "-7.25", "1e39", "0.3", "9999999999.9", "1.17549435e-38" };
        for (i = 0; i < (int)(sizeof t / sizeof t[0]); i++) {
            __L d; char *end; float w = strtof(t[i], 0);
            __strtof((char *)t[i], &d, &end);
            total++;
            if (bits(w) != (((uint32_t)d.hi << 16) | d.lo) && !(isinf(w))) { printf("FAIL strtof %s: got %04x%04x want %08x\n", t[i], d.hi, d.lo, bits(w)); fails++; }
        }
        for (i = 0; i < 30000; i++) {
            char buf[64]; __L d; float w; double v = (double)flt((rnd() & 0x807FFFFF) | ((90 + rnd() % 80) << 23));
            sprintf(buf, "%.*g", 1 + (int)(rnd() % 12), v);
            w = strtof(buf, 0);
            __strtof(buf, &d, 0);
            total++;
            if (bits(w) != (((uint32_t)d.hi << 16) | d.lo)) { if (fails < 25) printf("FAIL strtof %s: got %04x%04x want %08x\n", buf, d.hi, d.lo, bits(w)); fails++; }
        }
    }
    printf("%d checks, %d failed\n", total, fails);
    return fails != 0;
}
