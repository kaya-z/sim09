/* t_basic.c - arithmetic, loops, calls, recursion */
int printf(const char *fmt, ...);
int putchar(int c);

int fact(int n) { if (n <= 1) return 1; return n * fact(n - 1); }
int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
int gcd(int a, int b) { while (b) { int t = a % b; a = b; b = t; } return a; }
int sum(int n) { int i, s = 0; for (i = 1; i <= n; i++) s += i; return s; }
int g = 5;
int g2;

int main(void)
{
    int a = 7, b = 3, i;
    printf("%d %d %d %d %d\n", a + b, a - b, a * b, a / b, a % b);
    printf("%d %d %d\n", a & b, a | b, a ^ b);
    printf("%d %d %d\n", a << 2, a >> 1, -a >> 1);
    printf("%d %d %d %d\n", a < b, a > b, a == 7, a != 7);
    printf("%d %d %d\n", fact(6), fib(10), gcd(48, 18));
    printf("%d\n", sum(100));
    g2 = g * 3;
    printf("%d %d\n", g, g2);
    for (i = 0; i < 5; i++) putchar('a' + i);
    putchar('\n');
    i = 10;
    while (i > 0) { i -= 3; }
    printf("%d\n", i);
    do { i++; } while (i < 4);
    printf("%d\n", i);
    printf("%d %d %d\n", !0, !5, ~0);
    printf("%d %d\n", -a, +a);
    a += 5; a -= 2; a *= 3; a /= 2; a %= 7; a <<= 3; a >>= 1; a &= 0xFE; a |= 1; a ^= 5;
    printf("%d\n", a);
    printf("%d ", a++); printf("%d ", ++a); printf("%d ", a--); printf("%d\n", --a);
    printf("%d\n", (a > 3) ? 100 : 200);
    printf("%d %d\n", 3 + 4 * 5, (3 + 4) * 5);
    printf("%d %d\n", 100 / 7 * 7 + 100 % 7, 2 + 3 * 4 - 6 / 2);
    return 0;
}
