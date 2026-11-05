/* wc.c - count the lines, words and characters of a file:   cc wc.c   then   wc file */
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *f;
    int c, in = 0;
    unsigned lines = 0, words = 0, chars = 0;
    if (argc < 2) { puts("usage: wc file"); return 1; }
    f = fopen(argv[1], "r");
    if (!f) { printf("wc: cannot open %s\n", argv[1]); return 1; }
    while ((c = fgetc(f)) != EOF) {
        chars++;
        if (c == '\n') lines++;
        if (c == ' ' || c == '\n' || c == '\t') in = 0;
        else if (!in) { in = 1; words++; }
    }
    fclose(f);
    printf("%u %u %u %s\n", lines, words, chars, argv[1]);
    return 0;
}
