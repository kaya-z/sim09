/* t_ctl.c - switch, goto, break/continue, short circuit */
int printf(const char *fmt, ...);
int cnt;
int side(int v) { cnt++; return v; }
int classify(int n)
{
    switch (n) {
    case 0: return 100;
    case 1:
    case 2: return 200;
    case 5: n += 10;
    case 6: return n;
    default: return -1;
    }
}
int main(void)
{
    int i, j, s = 0;
    for (i = 0; i < 8; i++) printf("%d ", classify(i));
    printf("\n");
    for (i = 0; i < 10; i++) { if (i == 3) continue; if (i == 7) break; s += i; }
    printf("%d\n", s);
    for (i = 0; i < 3; i++) for (j = 0; j < 3; j++) { if (j == 2) break; s++; }
    printf("%d\n", s);
    i = 0;
loop:
    i++;
    if (i < 5) goto loop;
    printf("%d\n", i);
    cnt = 0;
    if (side(0) && side(1)) printf("no\n");
    if (side(1) || side(1)) printf("yes %d\n", cnt);
    printf("%d %d\n", side(0) || side(0), side(2) && side(3));
    printf("%d\n", cnt);
    i = (side(1), side(2), side(3));
    printf("%d %d\n", i, cnt);
    j = 5;
    switch (j) { case 5: printf("five\n"); break; default: printf("other\n"); }
    switch (j + 1) { case 1: printf("1\n"); break; case 2: printf("2\n"); }
    printf("end\n");
    return 0;
}
