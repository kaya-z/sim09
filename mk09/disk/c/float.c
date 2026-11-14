/* float.c - floating point (32 bits, IEEE single): sqrt, a series for pi, the printf formats %f %e %g */
#include <stdio.h>
#include <math.h>

int main(void)
{
    float x, pi = 0.0;
    int k;
    for (x = 1.0; x <= 5.0; x += 1.0) printf("sqrt(%g) = %f\n", x, sqrt(x));
    for (k = 0; k < 1000; k++) {                    /* pi/4 = 1 - 1/3 + 1/5 - ... */
        if (k % 2) pi -= 1.0 / (2 * k + 1);
        else pi += 1.0 / (2 * k + 1);
    }
    pi *= 4.0;
    printf("pi is about %f (%e), %g\n", pi, pi, pi);
    printf("floor(2.7) = %g, ceil(2.2) = %g, fabs(-1.5) = %g\n", floor(2.7), ceil(2.2), fabs(-1.5));
    return 0;
}
