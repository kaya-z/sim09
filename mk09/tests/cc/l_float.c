/* l_float.c - float / double (run only on MK09, where the library is the one in lib/cc; the expected output is that of gcc, so a double is used as a float here) */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

float sq(float x) { return x * x; }
float mix(int i, float f, long l) { return i + f + l; }
float g = 2.5;
float arr[3] = { 1.5, -2.25, 100000.0 };
struct S { float a; int b; float c; } st = { 0.5, 7, -8.75 };

int main(void)
{
    float a = 1.5, b = 0.25, c, d;
    double pi = 3.14159265, e;
    int i = 7, k;
    long l = 100000L;
    float *p = arr;

    printf("%f %f %f\n", a, b, a + b);
    printf("%f %f %f\n", a - b, a * b, a / b);
    printf("%.3f %.0f %.10f\n", pi, 2.5, b);
    printf("%e %E %g %g %g\n", 12345.678, 0.000123, 100000.0, 1000000.0, 0.0001);
    printf("%g %g %g\n", 1.0 / 3, 2.0 / 3, 123456789.0);
    printf("%8.2f|%-8.2f|%08.2f|%d\n", pi, pi, -pi, 5);
    printf("%d %d %d %d %d %d\n", a > b, a < b, a == 1.5, a != b, a >= 1.5, b <= 0.1);
    c = i;
    printf("%f %d\n", c, (int)(c * 2.9));
    d = l;
    printf("%f %ld\n", d, (long)(d / 3));
    k = -7.9;
    printf("%d %d %ld\n", k, (int)7.9, (long)-123456.7);
    printf("%f %f\n", -a, -(b + a));
    printf("%f %f %f\n", sq(1.1), mix(1, 0.5, 3L), sq(a) + sq(b));
    printf("%f %f %f\n", g, arr[1], p[2]);
    printf("%f %d %f\n", st.a, st.b, st.c);
    printf("%f %f\n", fabs(-3.5), sqrt(2.0));
    printf("%.4f %.4f\n", floor(-2.5), ceil(-2.5));
    e = pi * 2;
    printf("%f %f\n", e, e / pi);
    printf("%f %f %f\n", atof("3.75"), atof("-0.001"), atof("1e3"));
    a = 0;
    for (i = 0; i < 10; i++) a += 0.1;
    printf("%f %d\n", a, a == 1.0);
    if (a) printf("true\n");
    if (!(b - b)) printf("zero\n");
    c = 10;
    c++; ++c; c--;
    printf("%f\n", c);
    a = 1;
    while (a < 1000) a *= 2;
    printf("%f %s\n", a, a > 1000 ? "big" : "small");
    printf("%s|%.3s|%5.2s|\n", "abcdef", "abcdef", "abcdef");
    return 0;
}
