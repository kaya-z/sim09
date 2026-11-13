/* fib.c - recursion and a loop: the Fibonacci numbers (int is 16 bits: up to fib(23) = 28657; long is 32 bits) */
#include <stdio.h>

int fib(int n)
{
    return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

long fibl(int n)                    /* the loop: long, so that it goes on to fib(40) */
{
    long a = 0, b = 1, t;
    int i;
    for (i = 0; i < n; i++) { t = a + b; a = b; b = t; }
    return a;
}

int main(void)
{
    int i;
    for (i = 0; i <= 15; i++) printf("fib(%d) = %d\n", i, fib(i));
    printf("fib(40) = %ld\n", fibl(40));
    return 0;
}
