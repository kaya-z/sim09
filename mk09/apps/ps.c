/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/* ps - the tasks of the system (2.0: with the terminal and the user of the programs) */
#include "types.h"
#include "mklib.h"
#ifdef MK09_V2
#include "pwd.h"
#endif

static const char st_names[] = "free   ready  receivesend   sleep  zombie new    running";

int main(void)
{
    taskinfo_t ti;
    u8 t, k;
#ifdef MK09_V2
    u8 line, uid, run, lastuid = 0xFF;
    char nm[PW_NAME], last[PW_NAME];
    puts("TID STATE   PRIO TTY USER    REGION         SP    CPU");
#else
    puts("TID STATE   PRIO  REGION         SP    CPU");
#endif
    for (t = 0; t < NTASK; t++) {
        if (mk_tinfo(t, &ti) || ti.state == TS_FREE) continue;
        printf("%3d ", t);
        for (k = 0; k < 7; k++) putchar(st_names[ti.state * 7 + k]);
#ifdef MK09_V2
        if (taskuser(t, &line, &uid, &run) == 0 && run) {                       /* a program: its terminal and its user */
            if (uid != lastuid) { strcpy(last, unamestr(uid, nm)); lastuid = uid; }
            printf(" %d    %d   %-8s %04x-%04x  %04x  %u\n", ti.prio, line, last, ti.lo, ti.hi, ti.sp, ti.cpu);
        } else {                                                                 /* a server, or the idle task: the system */
            printf(" %d    -   system   %04x-%04x  %04x  %u\n", ti.prio, ti.lo, ti.hi, ti.sp, ti.cpu);
        }
#else
        printf(" %d    %04x-%04x  %04x  %u\n", ti.prio, ti.lo, ti.hi, ti.sp, ti.cpu);
#endif
    }
    return 0;
}
