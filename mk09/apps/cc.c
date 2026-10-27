/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * cc - the driver of the C compiler of MK09 (docs/IR.md)
 *
 *   cc [-S] [-k] [-v] [-o out] [-Dname[=value]] [-Idir] file.c
 *
 * file.c --cpp--> _.I --cc1--> _.ir --cg--> _.A (the start code, the program and the library routines it uses) --as--> out
 * The stages do not run under this program: the memory of a stage is the whole transient area, so each stage runs the next one in its own place
 * (exec, PM_EXEC).  This program only makes the command line  cpp ... ; cc1 ... ; cg ... ; as ... ; rm ...  and starts the first stage.
 *   -S   stop after cg (out is the assembler text; default: the name of the source with .s)     -k   keep the temporary files
 *   -v   print the command line     -o out   the output (default: the name of the source with .mx)
 * A command line is at most about 120 characters.
 */
#include "ccio.h"

#ifdef CC_HOST
#define putstr(s) fputs(s, stdout)
#endif

int main(int argc, char **argv)
{
    int i, n = 0;
    const char *src = NULL, *out = NULL, *dflag = NULL, *iflag = NULL;
    u8 stop_s = 0, keep = 0, verbose = 0;
    char *av[40];
    char outn[64];
    u16 len;

    for (i = 1; i < argc; i++) {
        char *a = argv[i];
        if (!strcmp(a, "-S")) stop_s = 1;
        else if (!strcmp(a, "-k")) keep = 1;
        else if (!strcmp(a, "-v")) verbose = 1;
        else if (!strcmp(a, "-o") && i + 1 < argc) out = argv[++i];
        else if (a[0] == '-' && a[1] == 'D') dflag = a;
        else if (a[0] == '-' && a[1] == 'I') iflag = a;
        else if (a[0] == '-') { puts("cc: unknown option"); return 2; }
        else src = a;
    }
    if (!src) { puts("usage: cc [-S] [-k] [-v] [-o out] [-Dname[=value]] [-Idir] file.c"); return 2; }
    len = (u16)strlen(src);
    if (len > 50) { puts("cc: the name of the file is too long"); return 2; }
    if (!out) {
        strcpy(outn, src);
        if (len > 2 && outn[len - 2] == '.') outn[len - 2] = 0;
        strcat(outn, stop_s ? ".s" : ".mx");
        out = outn;
    }
    av[n++] = "cc";
    av[n++] = ";";
    av[n++] = "cpp";
    if (dflag) av[n++] = (char *)dflag;
    if (iflag) av[n++] = (char *)iflag;
    av[n++] = (char *)src;
    av[n++] = "-o";
    av[n++] = "_.I";
    av[n++] = ";";
    av[n++] = "cc1";
    av[n++] = "_.I";
    av[n++] = ";";
    av[n++] = "cg";
    av[n++] = "-pp";
    av[n++] = "_.ir";
    if (!stop_s) av[n++] = "-L";
    av[n++] = "-o";
    av[n++] = stop_s ? (char *)out : "_.A";
    if (!stop_s) {
        av[n++] = ";";
        av[n++] = "as";
        av[n++] = "-o";
        av[n++] = (char *)out;
        av[n++] = "_.A";
    }
    if (!keep) {
        av[n++] = ";";
        av[n++] = "rm";
        av[n++] = "_.I";
        av[n++] = "_.ir";
        if (!stop_s) av[n++] = "_.A";
    }
    if (verbose) {
        for (i = 2; i < n; i++) { putstr(av[i]); putstr(av[i][0] == ';' ? "\n" : " "); }
        puts("");
    }
    return run_tail(n, av, 1);
}
