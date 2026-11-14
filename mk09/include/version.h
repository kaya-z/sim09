/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 YUKARI Semiconductor Devices */
/*
 * version.h - the version of MK09 (shown at boot and by `ver`; the documents carry the same number)
 *
 * Raise it with every change of the design or of the sources (docs/DESIGN.md, "改版履歴").
 *
 *   1.0  2026-10-15  first version (kernel, TTY / DISK / FS / PM, shell, jwp)
 *   1.1  2026-10-18  relocatable programs (MX version 2), memory manager (servers/mm.c), several
 *                    programs at the same time (`prog &`, kill, mem), console owner (TTYC_SETFG)
 *   1.2  2026-10-25  the memory manager is a task of its own (servers/mm.s, TID_MM = 5, protocol MM_ALLOC / MM_FREE / MM_INFO;
 *                    only PM may allocate and free); the tasks of the programs start at 6, NTASK 11.  (Backported from the
 *                    YSD9000 port, MK09 1.2.  The version is the one of the whole system, not of a server.)
 *   1.3  2026-10-25  the start-up message lists MM.  (YSD9000 1.3 also gave mk_tinfo the figures of the unused stack:
 *                    here the memory is flat and `stk` reads it itself, so that stays as it is.)
 *   1.4  2026-10-27  faster message round trip (3.1 ms -> 2.2 ms): the scheduler picks within the classes of the task numbers
 *                    (servers, programs, idle) instead of searching all the tasks for the best priority, a message is copied
 *                    by an unrolled loop, a receive does not look for blocked senders when there are none, the idle task is
 *                    made from the boot table.
 *   1.5  2026-10-27  the portable C kernel gets the same speed-ups in a form that does not depend on the CPU (a one-pass pick that
 *                    still looks at the priority values, arch_copymsg() that an architecture may replace, the send hint):
 *                    round trip 6.7 ms -> 5.0 ms.
 *   1.6  2026-10-27  from the YSD9000 port (its MK09 up to 1.13): pipes in the shell (`a | b | c` through /PIPE0.TMP and /PIPE1.TMP),
 *                    `more` reads its standard input, programs get up to 16 arguments, the shell says when the kernel stopped a program;
 *                    BASIC 1.1 (the name and version in the banner).  Not taken over: the development tools (as, ld, ar, cc, Pascal: they
 *                    make YSD9000 code), the timer fast path (the console server must poll the UART every tick here), the 8-block cache
 *                    of FS (no room in the 64 KB).
 *   1.7  2026-10-27  `as`: a 6809 assembler that runs on MK09 (programs written in assembler without a linker: the output is an MX
 *                    executable made with the start code of crt0app.s and a relocation table); apps/as.c, apps/as_text.h (the start code and a
 *                    small runtime: exit putc puts getc open close read write), samples in /ASM, tests (all instructions against lwasm).
 *   1.8  2026-10-27  the C compiler on MK09 (docs/IR.md): cpp, cc1 (C to the IR), cg (IR to 6809 assembler, with the library routines), the driver cc;
 *                    tools/irsim.py runs the IR on the PC.  PM_EXEC (a program is replaced by another one: the stages follow each other without a
 *                    program that waits for them, so each can use the whole transient area); programs get up to 32 arguments.
 *   1.9  2026-10-27  the C compiler, second step: JBxx branches in as (short when the label is near, found by repeating pass 1), cg uses them and the
 *                    flags that a load has set; a fault in cg (a push between a comparison and its branch); the headers (stdio string stdlib ctype stdarg
 *                    stddef mklib in /INC) and a bigger library (sprintf, strings, ctype, malloc, qsort, rand, seek, unlink, mkdir, chdir ...).
 *   1.10 2026-10-28  the C compiler, third step: FILE (fopen fclose fgetc fputc fgets fputs fread fwrite fseek ftell fprintf ...), `long` (32 bits, as objects
 *                    in memory whose operations are library calls), postinc (`*p++` is one load or store with ldb ,x+), the error messages of cc1 in
 *                    /LIB/CC1.MSG (tools/msgsplit.py), the symbol names of as in a pool (640 symbols).
 *   1.11 2026-10-28  the C compiler, fourth step: float and double (IEEE single precision, both 32 bits; operations are library calls, the constants are
 *                    made by cpp), printf %f %e %g with width and precision, atof strtod fabs sqrt floor ceil, <math.h>; the relocation table of as
 *                    is kept as it is written (gaps), local labels have 2 character scopes (820 symbols); the IR file can be over 64 KB.
 *   1.12 2026-10-28  Pascal, first step: pas1 (Pascal to the IR of the C compiler) and the driver pc; integer char boolean, enumerations, ranges, arrays, records,
 *                    pointers, nested procedures and functions (static link), var parameters, case with for repeat while, write/read, new/dispose.
 *   1.13 2026-10-28  Pascal: real and longint (library calls, like float and long of the C compiler), write/read of them, sqrt sin cos arctan ln exp trunc round ...;
 *                    as: local labels are kept as keys, not as text (1000 symbols).
 *   1.14 2026-10-28  Pascal: Turbo Pascal strings (string, string[n], + and comparisons, length copy pos concat upcase delete insert str val, functions that
 *                    return a string); the IR directive .stack N (cg) and {$M n} (the stack of the program).
 *   1.15 2026-10-28  Pascal: set (32 bytes, in and + - * = <> <= >=, include exclude), text files and file of T (assign reset rewrite append close seek eof eoln
 *                    filepos filesize, write(f, ...) read(f, ...)), goto and label, break continue exit, typed constants; the first stage pas0 (the lexer) is
 *                    separate from pas1 so that pas1 fits in the transient area.
 *   1.16 2026-10-28  Pascal: uses (skipped) and a few routines of the crt unit (clrscr gotoxy textcolor ...), random randomize paramcount paramstr, word shortint
 *                    smallint, {$M stack,heap}, output/input as the first argument; fixes (ord of an enum).
 *   1.17 2026-10-28  Pascal: procedural types and parameters (function(x: integer): integer; calls through variables, tables of procedures), char + char,
 *                    the parameter tables are shorter, a limit on the nesting of expressions and statements (the stack of pas1 is 700 bytes).
 *   1.18 2026-10-28  Pascal: units (uses name: the file name.pas is read, interface and implementation become declarations) and include files ({$I name}), both
 *                    done by pas0; the error messages are printed by a program of their own (pmsg).
 *   1.19 2026-10-28  faster compiles: the library files are sorted and have an index (tools/libsort.py, /LIB/CLIB.IDX); cg copies only the blocks it needs
 *                    (a compile of hello.pas: 590M cycles before, 150M now).
 *   1.20 2026-10-28  faster compiles, second step: as looks the instructions up by first letter and starts a far JBxx long (3 passes instead of 5),
 *                    cg stops the library rounds when nothing is needed and does not copy the marker lines (hello.pas: 56M cycles, a tenth of 1.18).
 *   1.21 2026-10-29  the library is assembled once, at build time (tools/mkobj.py, as -c): /LIB/CLIB.OBJ and RT.OBJ hold the code of the blocks; cg writes OBJ lines and as
 *                    links them (hello.pas: 30M cycles).  as: another round of pass 1 when a JBxx has an unknown target.
 *   1.22 2026-10-29  faster loading and linking: PM reads a program in chunks of 224 bytes (was 128; the data of MM moved to low memory for the room), cg skips the index lines
 *                    of blocks that are not needed and hashes its names (hello.pas: 24M cycles, 25 times faster than 1.18).
 *   1.23 2026-10-29  reboot (a program); PM loads a program with ONE request: FS_READTL, FS reads the file into the memory of the new task itself (FS is privileged for
 *                    tload); the code slots of TTY and FS were adjusted (cwidth is shorter).  hello.pas: 19M cycles.
 *   1.24 2026-10-29  -v prints the version and the copyright (as cpp cc1 cg cc pas0 pas1 pc); cc and pc print the command lines with -V now.
 *   1.25 2026-10-29  -v prints the version of the PROGRAM (TOOL_VERSION at the top of its source, one step per change of that program: as 1.10, cpp 1.4, cc1 1.6, cg 1.11, cc 1.1, pas0 1.4, pas1 1.8, pc 1.3): change it when you change that program, so that a
 *                    change of one tool can be followed without a new version of the system.
 *   1.26 2026-11-04  cc1 1.7: one walker for the initializers (global and local), loop_body; cc1 has 1062 bytes of the transient area left (was 322).
 *   1.27 2026-11-04  YSD Pascal (the name of the Pascal compiler): variant records (case in a record) and range checks ({$R+}: array indexes and subrange assignments, __prchk); pas0 1.5, pas1 1.9.
 *   1.28 2026-11-05  the console receive interrupt: the kernel (IRQ) puts the characters of the ACIA into the ring of the console server (SYS_RXREG, NFY_RX); the server no longer polls the ACIA.
 *                    The board has a FIFO of 256 characters for the time that the CPU is held (a write to the card); a character that finds the ring (128) full is lost.  TPA_BASE stays $3C00 (K_CSZ, TTY_CSZ, FS_CSZ resized).
 *   1.29 2026-11-06  BASIC 1.2, taken over from the YSD9000 port (its MK09 1.15): FOR / NEXT with a step of 1 or -1 on whole numbers add to the bits of the variable and compare the words, NEXT returns to the statement
 *                    after the colon, an IF finds its ELSE once, GOTO / GOSUB / THEN n remember the line, "variable <op> variable or number" is one operation; run() keeps pc and sp in registers
 *                    (run_cold, run_cmp); disk/bas/tfor.bas, tif.bas and the loop benchmarks bfor bif bmix bnop.  The code of basic is 1.8 KB bigger (cmoc), so the program area is 2400 bytes (was 3800)
 *                    and the arrays 220 elements (was 300); cmoc miscompiles a ?: of longs, which the YSD9000 sources use (f_cmp, run_cmp, NEXT): if / else there.
 *   1.30 2026-11-06  BASIC 1.3: Break.  While a program runs the console is raw without echo and without the ^C of the shell, and execute() looks at the keys every 16 statements; a ^C stops the program with
 *                    "?Break in n" and basic goes on at Ok (INPUT and the prompt use the console as before).  Program area 2200 bytes (was 2400).
 *   1.31 2026-11-06  shutdown and halt print "CPU halted." (PM, after the sync, before the power register is written).
 *   1.32 2026-11-07  the disk server uses the asynchronous mode of the disk port ($E049): the command returns at once, the kernel IRQ turns the interrupt off and notifies
 *                    the disk server (NFY_DISK), which waits in mk_recv, so the other tasks run while the SD card works.  The boot ROM and the C kernel were adapted; the boot code
 *                    of the kernel (section start) is loaded into the transient area (it runs once), which gave back 185 bytes of the slot of the kernel.
 *   1.33 2026-11-12  the file system up to 2GB (FAT16): a volume and a file are no longer limited to 32MB.  fs_core.c names a block by (cluster, block in it) and makes the 23 bit disk number only in blk_io;
 *                    a position and a size are (cluster of the file, byte in it); the directory cursor and the file cursor share their place in the handle (the FS data slot stays).  FS_CSZ +157 bytes (TPA_BASE stays $3C00).
 *                    df shows MB; tools/mkdisk.py makes volumes up to 2GB (the clusters get bigger: --spc); tests: fs_host big (files over 32MB), fstest on volumes of 30MB .. 600MB, mk09_test a volume of 128MB.
 *   1.34 2026-11-13  `mem` shows the memory map of the machine (the slots of the system code, the transient area, the I/O page, the high memory) and the memory of the programs in KB: total, used, free (mem 1.1).  No system change (TPA_BASE stays $3C00).
 *   1.35 2026-11-13  the FM-11 port (BOARD=fm11, bsp/fm11, fm11/, memmap_fm11.mk; 2HD floppy and hard disk boot) and its commands `format` (low level format of a 2HD floppy: DISK_FORMAT) and `mount` / `umount` (the floppy disks 1-4 as "N:/path": FS_MOUNT, FS_STATFS with a drive, df N).  Backported from the port; the code for several volumes is compiled only for BOARD_FM11 (the FS slot of the sbc09 is full: TPA_BASE stays $3C00).
 *   1.36 2026-11-14  the shell: ^C drops the rest of a script (source) and of a pipeline, not only the program that runs (the status 130 of a program that PM stopped).
 *   1.37 2026-11-14  more 1.1: `more` alone (the standard input is the console) prints the usage and ends; it used to read the keys as the text and there was no way back to the shell.
 */
#ifndef MK_VERSION_H
#define MK_VERSION_H

#define MK09_VERSION    "1.37"
#ifdef BOARD_FM11
#define MK09_FOR        " (for FM-11)"      /* the version of a machine dependent system or program ends with this */
#ifdef FM11_MMU
#define MK09_MEM        "Level 2: 128KB, MMU"
#else
#define MK09_MEM        "Level 1: 64KB flat, no MMU"
#endif
#else
#define MK09_FOR        ""
#define MK09_MEM        "64KB flat, no MMU"
#endif
#define MX_VERSION      2           /* version of the MX executable format (docs/SPEC.md) */

#endif
