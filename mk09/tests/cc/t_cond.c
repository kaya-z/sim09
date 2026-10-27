/* t_cond.c - conditions while other values wait on the stack (the pushes must not come between a comparison and its branch) */
int printf(const char *fmt, ...);
int idf(int x) { return x; }
int add(int a, int b) { return a + b; }
int (*fp)(int);
int g = 7;
int main(void)
{
    int z = 0, o = 1, a = 3, b = 5;
    fp = idf;
    printf("%d %d\n", fp(z ? 10 : 20), fp(o ? 10 : 20));
    printf("%d %d\n", add(g, z ? 1 : 2), add(g, a < b ? 100 : 200));
    printf("%d %d\n", g + (a > b ? 1 : 2), g + (a == 3 ? 40 : 50));
    printf("%d\n", g * (z ? 2 : (o ? 3 : 4)));
    printf("%d %d\n", add(a, b) + (a && b ? 1 : 0), add(a, b) + (a && z ? 1 : 0));
    printf("%d\n", fp(g) + (z || o ? 10 : 20));
    return 0;
}
