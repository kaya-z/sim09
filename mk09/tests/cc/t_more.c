/* t_more.c - nested calls, local initializers, pointer compound assignments, unsigned, sieve, sort */
int printf(const char *fmt, ...);
int putchar(int c);
int max3(int a, int b, int c) { return a > b ? (a > c ? a : c) : (b > c ? b : c); }
int add3(int a, int b, int c) { return a * 100 + b * 10 + c; }
void bsort(int *a, int n)
{
    int i, j, t;
    for (i = 0; i < n - 1; i++)
        for (j = 0; j < n - 1 - i; j++)
            if (a[j] > a[j + 1]) { t = a[j]; a[j] = a[j + 1]; a[j + 1] = t; }
}
int sieve(int n)
{
    char flags[50];
    int i, j, count = 0;
    for (i = 0; i < n; i++) flags[i] = 1;
    for (i = 2; i < n; i++)
        if (flags[i]) { count++; for (j = i + i; j < n; j += i) flags[j] = 0; }
    return count;
}
struct pair { int a; char c; };
struct pair pairs[3] = { { 1, 'x' }, { 2, 'y' }, { 3, 'z' } };
int strcmp2(char *a, char *b) { while (*a && *a == *b) { a++; b++; } return *a - *b; }
unsigned usub(unsigned a, unsigned b) { return a - b; }

int main(void)
{
    int arr[6] = { 5, 2, 9, 1, 7, 3 };
    char s[] = "abc";
    char t[8] = "xy";
    int *p = arr;
    int i;
    unsigned u = 3, v = 5;
    printf("%d\n", add3(max3(1, 2, 3), max3(9, 4, 7), max3(add3(0, 1, 2), 5, 6)));
    bsort(arr, 6);
    for (i = 0; i < 6; i++) printf("%d ", arr[i]);
    printf("\n%d\n", sieve(50));
    p += 2;
    printf("%d ", *p);
    p -= 1;
    printf("%d ", *p);
    p = p + 3;
    printf("%d %d\n", *p, p - arr);
    printf("%s %s %d %d\n", s, t, t[2], sizeof(t));
    for (i = 0; i < 3; i++) printf("%d%c ", pairs[i].a, pairs[i].c);
    printf("\n%d %d\n", strcmp2("abc", "abd"), strcmp2("b", "a"));
    printf("%d %d %d\n", u < v, u > v, (int)(u - v) < 0);
    printf("%u %u\n", usub(3, 5), usub(5, 3));
    printf("%d %d %d\n", 1 << 4, 0x8000 >> 15, (unsigned)0x8000 >> 15);
    printf("%d\n", ((1 + 2) * (3 + 4) - 5) / 2 % 5);
    i = 3;
    i = i++ + 2;
    printf("%d\n", i);
    {
        int *q = &arr[1];
        *q += 100;
        (*q)++;
        printf("%d %d\n", arr[1], *q);
        q[1] = q[0] > 100 ? 7 : 8;
        printf("%d\n", arr[2]);
    }
    return 0;
}
