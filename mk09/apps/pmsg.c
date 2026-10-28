/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * pmsg - prints an error message of a compiler:   pmsg messages.msg file line n
 *
 * pas1 has no room for the code that reads its messages (/LIB/PAS1.MSG: line n is message n), so it runs this program in its own place when it finds an error.
 * The exit code is 1, which stops the chain of the stages.
 */
#include "ccio.h"

int main(int argc, char **argv)
{
    rfile r;
    int c, n = 1, want = argc > 4 ? atoi_(argv[4]) : 0;
    if (argc < 5) return 2;
    putstr(argv[2]); putchar(':'); putstr(argv[3]); putstr(": ");
    if (!r_open(&r, argv[1])) {
        while (n < want && (c = r_getc(&r)) >= 0) if (c == '\n') n++;
        while ((c = r_getc(&r)) >= 0 && c != '\n') putchar(c);
        r_close(&r);
    } else { putstr("error "); putstr(argv[4]); }
    putchar('\n');
    return 1;
}
