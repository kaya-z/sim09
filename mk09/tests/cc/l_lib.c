/* l_lib.c - the headers and the library of the compiler (run only on MK09, where the library is the one in lib/cc; the expected output is that of gcc) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>

int sum(int n, ...)
{
    va_list ap;
    int s = 0;
    va_start(ap, n);
    while (n--) s += va_arg(ap, int);
    va_end(ap);
    return s;
}

int cmp_int(char *a, char *b) { return *(int *)a - *(int *)b; }

int main(void)
{
    char buf[40];
    char *p, *q;
    int i, v[6] = { 5, 3, 9, 1, 7, 2 };
    printf("[%5d][%-5d][%05d][%x][%u][%c][%s][%8s][%-8s]\n", 42, 42, 42, 255, 65535, 'z', "str", "right", "left");
    printf("[%05d][%-5d][%5d]\n", -42, -42, -42);
    sprintf(buf, "%d-%s-%d", 12, "ab", -7);
    printf("%s %d\n", buf, (int)strlen(buf));
    strcpy(buf, "hello");
    strcat(buf, ", world");
    printf("%s %d %d %d\n", buf, strcmp("abc", "abd") < 0, strncmp("abcd", "abxx", 2), memcmp("ab", "ab", 2));
    p = strchr(buf, 'w');
    q = strrchr(buf, 'o');
    printf("%s %s %s %d\n", p, q, strstr(buf, "lo,"), strstr(buf, "xyz") == NULL);
    memset(buf, 'x', 3);
    buf[3] = 0;
    printf("%s %s\n", buf, strncpy(buf, "ab", 3));
    memmove(buf + 1, buf, 2);
    buf[3] = 0;
    printf("%s\n", buf);
    printf("%d%d%d%d%d %c%c %d%d\n", !!isdigit('7'), !!isalpha('7'), !!isalnum('q'), !!isspace('\t'), !!isupper('Q'), toupper('a'), tolower('Q'), !!isxdigit('f'), !!ispunct('!'));
    printf("%d %d %d %d\n", abs(-12), atoi("  -345x"), atoi("+8"), atoi("abc"));
    printf("%d\n", sum(4, 1, 2, 3, 4));
    qsort((char *)v, 6, sizeof(int), cmp_int);
    for (i = 0; i < 6; i++) printf("%d ", v[i]);
    printf("\n");
    p = malloc(100);
    q = malloc(50);
    strcpy(p, "first");
    strcpy(q, "second");
    printf("%s %s\n", p, q);
    free(p);
    p = malloc(60);
    printf("%d\n", p != NULL && p != q);
    free(q);
    free(p);
    p = calloc(10, 2);
    for (i = 0, q = p; i < 20; i++) if (q[i] != 0) break;
    printf("%d\n", i);
    srand(7);
    i = rand();
    printf("%d\n", i >= 0);
    {
        FILE *f = fopen("ftest.txt", "w");
        char line[40];
        int c;
        fprintf(f, "line %d\n", 1);
        fputs("second line\n", f);
        fputc('x', f); fputc('y', f); fputc('\n', f);
        fclose(f);
        f = fopen("ftest.txt", "r");
        fgets(line, 40, f);
        printf("[%s]", line);
        c = fgetc(f);
        ungetc(c, f);
        printf("%c %d\n", c, ftell(f));
        fgets(line, 40, f);
        printf("[%s]", line);
        fseek(f, 5, 0);
        printf("%c", fgetc(f));
        fseek(f, 0, 2);
        printf(" %d %d\n", ftell(f), fgetc(f) == EOF);
        printf("%d\n", feof(f) != 0);
        fclose(f);
        f = fopen("ftest.txt", "a");
        fprintf(f, "tail\n");
        fclose(f);
        f = fopen("ftest.txt", "r");
        printf("%d ", (int)fread(line, 1, 30, f));
        fclose(f);
        printf("%d %d\n", remove("ftest.txt"), fopen("ftest.txt", "r") == NULL);
        fprintf(stdout, "to stdout %d\n", 5);
    }
    return 0;
}
