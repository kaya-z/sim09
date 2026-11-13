/* primes.c - the sieve of Eratosthenes: an array, nested loops.  primes [limit]  (default 200) */
#include <stdio.h>
#include <stdlib.h>

char sieve[2001];

int main(int argc, char **argv)
{
    int limit = 200, i, j, n = 0;
    if (argc > 1) limit = atoi(argv[1]);
    if (limit > 2000) limit = 2000;
    for (i = 2; i <= limit; i++) sieve[i] = 1;
    for (i = 2; i * i <= limit; i++)
        if (sieve[i])
            for (j = i * i; j <= limit; j += i) sieve[j] = 0;
    for (i = 2; i <= limit; i++)
        if (sieve[i]) { printf("%d ", i); n++; }
    printf("\n%d primes up to %d\n", n, limit);
    return 0;
}
