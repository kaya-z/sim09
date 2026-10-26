/* t_ptr.c - pointers, arrays, strings */
int printf(const char *fmt, ...);
int puts(const char *s);

int len(char *s) { int n = 0; while (*s++) n++; return n; }
void rev(char *s) { char *e = s; char t; while (*e) e++; e--; while (s < e) { t = *s; *s++ = *e; *e-- = t; } }
int sumarr(int *a, int n) { int i, s = 0; for (i = 0; i < n; i++) s += a[i]; return s; }
void swap(int *x, int *y) { int t = *x; *x = *y; *y = t; }
int tab[5] = { 10, 20, 30, 40, 50 };
char msg[] = "hello";
char *names[] = { "one", "two", "three" };
int m[2][3] = { { 1, 2, 3 }, { 4, 5, 6 } };

int main(void)
{
    int a[4];
    int i, x = 1, y = 2;
    int *p;
    char buf[20];
    char *q;
    for (i = 0; i < 4; i++) a[i] = i * i;
    printf("%d %d\n", sumarr(a, 4), sumarr(tab, 5));
    swap(&x, &y);
    printf("%d %d\n", x, y);
    p = a;
    p++;
    printf("%d %d %d\n", *p, p[1], *(p + 2));
    printf("%d\n", p - a);
    printf("%d %d\n", len(msg), len("a longer string"));
    for (i = 0; msg[i]; i++) buf[i] = msg[i];
    buf[i] = 0;
    rev(buf);
    puts(buf);
    for (i = 0; i < 3; i++) printf("%s ", names[i]);
    printf("\n%d %d %d\n", m[0][0], m[1][2], m[1][1] + m[0][2]);
    q = "pointer";
    printf("%c%c %s\n", q[0], *(q + 1), q + 3);
    printf("%d %d\n", sizeof(a), sizeof(int *));
    printf("%d\n", sizeof(m) / sizeof(m[0]));
    p = &tab[3];
    printf("%d %d\n", *p, p > &tab[1]);
    *p = 99;
    printf("%d\n", tab[3]);
    return 0;
}
