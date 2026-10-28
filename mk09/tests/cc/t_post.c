/* t_post.c - p++ and i++ as values (the postinc instruction and its use with a load or a store) */
int printf(const char *fmt, ...);
struct pt { int x, y, z; };
char dst[20];
int arr[5] = { 10, 20, 30, 40, 50 };
char *gs = "global";
int gi = 5;
void copy(char *d, char *s) { while (*d++ = *s++) ; }
int sum(int *p, int n) { int s = 0; while (n--) s += *p++; return s; }
int len(char *s) { char *p = s; while (*p++) ; return p - s - 1; }
int main(void)
{
    char *s = "hello", *d = dst;
    int *ip = arr, i = 0, j, k;
    struct pt pts[3], *pp = pts;
    copy(dst, s);
    printf("%s %d\n", dst, len(dst));
    printf("%c%c%c ", *s++, *s++, *s++);
    printf("%s\n", s);
    printf("%d %d %d\n", *ip++, *ip++, *ip);
    printf("%d\n", sum(arr, 5));
    j = i++;
    k = i++;
    printf("%d %d %d\n", j, k, i);
    printf("%d %d\n", arr[i++], arr[i++]);
    printf("%d %d\n", gi++, gi);
    gi--;
    printf("%d %d\n", gi--, gi);
    for (j = 0; j < 3; j++) { pp->x = j; pp++->y = j * 2; }
    printf("%d %d %d\n", pts[0].y, pts[1].y, pts[2].y);
    d = dst;
    *d++ = 'A'; *d++ = 'B'; *d++ = 0;
    printf("%s\n", dst);
    printf("%c ", *gs++);
    printf("%s\n", gs);
    j = 0;
    j++;
    ip = arr + 2;
    printf("%d %d\n", j, *ip--);
    printf("%d\n", *ip);
    return 0;
}
