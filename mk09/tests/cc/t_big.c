/* t_big.c - long jumps (bodies bigger than 127 bytes), comparisons with 0 after arithmetic */
int printf(const char *fmt, ...);
int g[8];
int sgn(int x) { if (x < 0) return -1; if (x > 0) return 1; return 0; }
int flags(int x) { int r = 0; if (x == 0) r += 1; if (x != 0) r += 2; if (x < 0) r += 4; if (x >= 0) r += 8; if (x <= 0) r += 16; if (x > 0) r += 32; return r; }
int sub1(int a, int b) { int d = a - b; if (d < 0) return -1; if (d == 0) return 0; return 1; }
int main(void)
{
    int i, s = 0, t = 0;
    unsigned u = 5;
    for (i = 0; i < 6; i++) {
        s += i * 1 + g[1];
        s += i * 2 + g[2];
        s += i * 3 + g[3];
        s += i * 4 + g[4];
        s += i * 5 + g[5];
        s += i * 6 + g[6];
        s += i * 7 + g[7];
        s += i * 8 + g[0];
        s += i * 9 + g[1];
        s += i * 10 + g[2];
        s += i * 11 + g[3];
        s += i * 12 + g[4];
        s += i * 13 + g[5];
        s += i * 14 + g[6];
        s += i * 15 + g[7];
        s += i * 16 + g[0];
        s += i * 17 + g[1];
        s += i * 18 + g[2];
        s += i * 19 + g[3];
        s += i * 20 + g[4];
        s += i * 21 + g[5];
        s += i * 22 + g[6];
        s += i * 23 + g[7];
        s += i * 24 + g[0];
        s += i * 25 + g[1];
        s += i * 26 + g[2];
        s += i * 27 + g[3];
        s += i * 28 + g[4];
        s += i * 29 + g[5];
        s += i * 30 + g[6];
        s += i * 31 + g[7];
        s += i * 32 + g[0];
        s += i * 33 + g[1];
        s += i * 34 + g[2];
        s += i * 35 + g[3];
        s += i * 36 + g[4];
        s += i * 37 + g[5];
        s += i * 38 + g[6];
        s += i * 39 + g[7];
        s += i * 40 + g[0];
        if (i == 3) {
            t += (s & 255) - 1;
            t += (s & 255) - 2;
            t += (s & 255) - 3;
            t += (s & 255) - 4;
            t += (s & 255) - 5;
            t += (s & 255) - 6;
            t += (s & 255) - 7;
            t += (s & 255) - 8;
            t += (s & 255) - 9;
            t += (s & 255) - 10;
            t += (s & 255) - 11;
            t += (s & 255) - 12;
            t += (s & 255) - 13;
            t += (s & 255) - 14;
            t += (s & 255) - 15;
            t += (s & 255) - 16;
            t += (s & 255) - 17;
            t += (s & 255) - 18;
            t += (s & 255) - 19;
            t += (s & 255) - 20;
            t += (s & 255) - 21;
            t += (s & 255) - 22;
            t += (s & 255) - 23;
            t += (s & 255) - 24;
            t += (s & 255) - 25;
            t += (s & 255) - 26;
            t += (s & 255) - 27;
            t += (s & 255) - 28;
            t += (s & 255) - 29;
            t += (s & 255) - 30;
        } else {
            t -= i + 1;
            t -= i + 2;
            t -= i + 3;
            t -= i + 4;
            t -= i + 5;
            t -= i + 6;
            t -= i + 7;
            t -= i + 8;
            t -= i + 9;
            t -= i + 10;
            t -= i + 11;
            t -= i + 12;
            t -= i + 13;
            t -= i + 14;
            t -= i + 15;
            t -= i + 16;
            t -= i + 17;
            t -= i + 18;
            t -= i + 19;
            t -= i + 20;
            t -= i + 21;
            t -= i + 22;
            t -= i + 23;
            t -= i + 24;
            t -= i + 25;
            t -= i + 26;
            t -= i + 27;
            t -= i + 28;
            t -= i + 29;
            t -= i + 30;
        }
    }
    printf("%d %d\n", s, t);
    for (i = -2; i <= 2; i++) printf("%d:%d,%d ", i, sgn(i), flags(i));
    printf("\n%d %d %d\n", sub1(3, 5), sub1(5, 5), sub1(7, 5));
    while (u > 0) { u--; if (u == 2) break; }
    printf("%u\n", u);
    return 0;
}
