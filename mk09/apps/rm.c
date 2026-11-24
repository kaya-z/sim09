/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* rm - delete files and empty directories */
#include "types.h"
#include "mklib.h"
#ifdef MK09_V2
#include "mkstr.h"
#endif

int main(int argc, char **argv)
{
    int i, r, rc = 0;
    if (argc < 2) { puts("usage: rm file ..."); return 1; }
#ifdef MK09_V2
    if (argc == 3 && !strcmp(argv[1], "-t") && strlen(argv[2]) < 16) {     /* rm -t base: the temporary files of cc (base.I base.ir base.A; the command line of a stage is short) */
        char n[24];
        static const char *ext[3] = { ".I", ".ir", ".A" };
        for (i = 0; i < 3; i++) { strcpy(n, argv[2]); strcat(n, ext[i]); unlink(n); }
        return 0;
    }
#endif
    for (i = 1; i < argc; i++) {
        r = unlink(argv[i]);
        if (r < 0) { printf("rm: %s: %s\n", argv[i], strerr_(r)); rc = 1; }
    }
    return rc;
}
