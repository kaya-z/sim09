/* t_func.c - static, globals, char and unsigned, macros */
int printf(const char *fmt, ...);
#define SQ(x) ((x) * (x))
#define N 4
#define MAX(a, b) ((a) > (b) ? (a) : (b))
static int counter;
int next(void) { static int n = 10; n++; counter++; return n; }
int ch(char c) { return c; }
char gc = -3;
unsigned char guc = 200;
int main(void)
{
    char c = 200;
    unsigned char uc = 200;
    int i;
    printf("%d %d %d\n", next(), next(), next());
    printf("%d\n", counter);
    printf("%d %d %d %d\n", c, uc, gc, guc);
    printf("%d\n", ch(65));
    printf("%d %d\n", SQ(N + 1), MAX(3, 9));
    c = 65;
    c += 2;
    printf("%c %d\n", c, c);
    for (i = 0; i < N; i++) printf("%d", i * SQ(i));
    printf("\n");
    printf("%d %d\n", (char)321, (unsigned char)(-1));
    printf("%u %u\n", (unsigned)65535, 40000);
    return 0;
}
