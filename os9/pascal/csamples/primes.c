/* primes.c - the prime numbers below 200 (sieve of Eratosthenes):   cc primes.c   then   primes */
#include <stdio.h>

char sieve[200];

int main(void)
{
    int i, j, n = 0;
    for (i = 2; i < 200; i++) {
        if (sieve[i]) continue;
        printf("%d ", i);
        n++;
        for (j = i + i; j < 200; j += i) sieve[j] = 1;
    }
    printf("\n%d primes\n", n);
    return 0;
}
