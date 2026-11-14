/* sort.c - function pointers: qsort with two comparison functions (numbers up, numbers down), and rand */
#include <stdio.h>
#include <stdlib.h>

int up(char *a, char *b) { return *(int *)a - *(int *)b; }
int down(char *a, char *b) { return *(int *)b - *(int *)a; }

void show(char *what, int *v, int n)
{
    int i;
    printf("%s:", what);
    for (i = 0; i < n; i++) printf(" %d", v[i]);
    printf("\n");
}

int main(void)
{
    int v[12], i;
    srand(7);
    for (i = 0; i < 12; i++) v[i] = rand() % 100;
    show("random", v, 12);
    qsort((char *)v, 12, sizeof(int), up);
    show("up    ", v, 12);
    qsort((char *)v, 12, sizeof(int), down);
    show("down  ", v, 12);
    return 0;
}
