/* fcopy.c - a binary copy with fread and fwrite.   fcopy from to */
#include <stdio.h>

int main(int argc, char **argv)
{
    FILE *in, *out;
    char buf[256];
    unsigned n, total = 0;

    if (argc != 3) { puts("usage: fcopy from to"); return 1; }
    in = fopen(argv[1], "r");
    if (!in) { printf("fcopy: cannot open %s\n", argv[1]); return 1; }
    out = fopen(argv[2], "w");
    if (!out) { printf("fcopy: cannot create %s\n", argv[2]); fclose(in); return 1; }
    while ((n = fread(buf, 1, 256, in)) > 0) {
        fwrite(buf, 1, n, out);
        total += n;
    }
    fclose(in);
    fclose(out);
    printf("%u bytes copied\n", total);
    return 0;
}
