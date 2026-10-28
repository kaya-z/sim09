/* l_long.c - long (run only on MK09, where the library is the one in lib/cc; the expected output is that of gcc, so no value goes over 32 bits) */
#include <stdio.h>
#include <stdlib.h>

long fact(int n) { long r = 1; while (n > 1) r *= n--; return r; }
long add3(long a, long b, long c) { return a + b + c; }
unsigned long triple(unsigned long x) { return x * 3; }
long idiv(long a, int b) { return a / b; }
int isbig(long v) { return v > 100000L; }
long g = 100000L;
long neg = -1500000L;
unsigned long ug = 3000000000UL;
long arr[3] = { 1L, -2L, 300000L };
struct S { long a; int b; long c; } st = { 70000L, 5, -70000L };

int main(void)
{
    long a = 123456L, b = 1000, c, d;
    unsigned long u = 4000000000UL, w;
    int i = 7, k;
    char ch = 5;

    printf("%ld %ld %ld\n", a, b, a + b);
    printf("%ld %ld %ld %ld\n", a - b, a * b, a / b, a % b);
    printf("%ld %ld %ld\n", a & 0xFFFFL, a | 0x10000000L, a ^ 0xFFFFL);
    printf("%ld %ld %ld %ld\n", a << 4, a >> 3, -a, ~a);
    printf("%ld %ld\n", neg >> 4, neg / 7);
    printf("%d %d %d %d %d %d\n", a > b, a < b, a == 123456L, a != b, a >= 123456L, b <= 999);
    printf("%d %d %d\n", neg < 0, neg > -2000000L, a > 65536);
    c = a;
    c += 1000000L;
    c -= b;
    c *= 3;
    c /= 5;
    c %= 100000L;
    printf("%ld\n", c);
    c = 5;
    c <<= 20;
    c >>= 3;
    c |= 7;
    c &= 0xFFFFFL;
    c ^= 1;
    printf("%ld\n", c);
    c = i;
    d = c++;
    printf("%ld %ld ", c, d);
    d = ++c;
    printf("%ld %ld ", c, d);
    d = c--;
    printf("%ld %ld ", c, d);
    d = --c;
    printf("%ld %ld\n", c, d);
    c = 65535;
    c++;
    printf("%ld ", c);
    c--;
    c--;
    printf("%ld\n", c);
    printf("%ld %ld %ld %ld\n", a + i, i + a, a * i, 100000L - i);
    printf("%ld %ld\n", a + ch, (long)i * 100000);
    k = (int)(a & 0x7FFFL);
    printf("%d %d %d\n", k, (int)(a >> 16), (char)a);
    printf("%ld %ld %ld\n", (long)-5, (long)ch, (long)(i * 1000));
    printf("%lu %lu %lu\n", u, u / 3, u % 1000);
    w = u >> 4;
    printf("%lu %lx %lu\n", w, w, triple(1000000000UL));
    printf("%d %d\n", u > 3000000000UL, u > a);
    printf("%ld %ld %ld\n", fact(7), fact(10), add3(100000L, 200000L, -50L));
    printf("%ld %d %d\n", idiv(1000000L, 7), isbig(99999L), isbig(100001L));
    printf("%ld %ld %lu\n", g, neg, ug);
    printf("%ld %ld %ld\n", arr[0], arr[1], arr[2] + arr[1]);
    arr[1] = arr[2] * 2;
    printf("%ld %ld %ld %d\n", arr[1], st.a + st.c, st.c, st.b);
    st.a += 5;
    printf("%ld\n", st.a);
    printf("%ld %ld\n", a ? 1L : 2L, !a ? 1L : 2L);
    printf("%d %d\n", !a, !(a - a));
    c = 3;
    k = 0;
    while (c) { c--; k++; }
    printf("%d\n", k);
    if (a && b) printf("both\n");
    if (!(a - a) || 0) printf("zero\n");
    printf("%ld %ld %ld\n", atol("-123456"), atol("  98765x"), labs(-77777L));
    printf("[%8ld][%-8ld][%08ld][%lx][%lx]\n", 12345L, 12345L, -12345L, 0xABCDEL, 255L);
    printf("%d\n", (int)sizeof(long) >= 4);
    c = 0x12345678L;
    printf("%lx %lx\n", c, c >> 16);
    return 0;
}
