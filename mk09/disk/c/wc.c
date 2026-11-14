/* wc.c - files: fopen, fgets, fclose.  Counts the lines, words and characters.   wc file ... */
#include <stdio.h>
#include <ctype.h>

int main(int argc, char **argv)
{
    FILE *f;
    char line[120], *p;
    int a, lines, words, chars, inword;

    if (argc < 2) { puts("usage: wc file ..."); return 1; }
    for (a = 1; a < argc; a++) {
        f = fopen(argv[a], "r");
        if (!f) { printf("wc: cannot open %s\n", argv[a]); continue; }
        lines = words = chars = 0;
        while (fgets(line, 120, f)) {
            lines++;
            inword = 0;
            for (p = line; *p; p++) {
                chars++;
                if (isspace(*p)) inword = 0;
                else if (!inword) { inword = 1; words++; }
            }
        }
        fclose(f);
        printf("%5d %5d %5d  %s\n", lines, words, chars, argv[a]);
    }
    return 0;
}
