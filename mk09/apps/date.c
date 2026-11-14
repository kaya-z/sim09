/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * date - show the date and time of the clock;  date -s YYYY-MM-DD [HH:MM[:SS]]  sets the clock (FM-11: it has no calendar chip and no battery,
 * the clock of the system counts from 2026-01-01 at every boot; on the sbc09 the clock is read-only)
 */
#include "types.h"
#include "mklib.h"
#include "mkstr.h"
#include "mk09.h"
#include "version.h"
#define TOOL_VERSION "1.1"

int main(int argc, char **argv)
{
    u16 d, t;
    if (argc >= 2 && !strcmp(argv[1], "-v")) {
        printf("date %s%s\n", TOOL_VERSION, MK09_FOR);
        return 0;
    }
    if (argc >= 2 && !strcmp(argv[1], "-s")) {
        u16 n[6];
        u8 k = 0, i, in = 0;
        int r;
        const char *p;
        for (i = 0; i < 6; i++) n[i] = 0;
        for (i = 2; i < argc; i++)                      /* the numbers of the arguments, divided by any non-digit */
            for (p = argv[i];; p++) {
                if (*p >= '0' && *p <= '9') {
                    if (!in) { in = 1; if (k < 6) k++; }
                    if (k <= 6) n[k - 1] = n[k - 1] * 10 + (*p - '0');
                } else {
                    in = 0;
                    if (!*p) break;
                }
            }
        if (k < 3 || n[0] < 1980 || n[0] > 2107) { puts("usage: date -s YYYY-MM-DD [HH:MM[:SS]]"); return 1; }
        d = ((n[0] - 1980) << 9) | (n[1] << 5) | n[2];
        t = (n[3] << 11) | (n[4] << 5) | (n[5] >> 1);
        r = settime(d, t);
        if (r) { puts(r == -E_INVAL ? "date: bad date or time" : "date: the clock cannot be set"); return 1; }
    }
    if (gettime(&d, &t) < 0) { puts("date: no clock"); return 1; }
    printf("%04d-%02d-%02d %02d:%02d:%02d\n", 1980 + (d >> 9), (d >> 5) & 15, d & 31, t >> 11, (t >> 5) & 63, (t & 31) * 2);
    return 0;
}
