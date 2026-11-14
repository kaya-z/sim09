# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# memmap_fm11_l2.mk - the memory map of the FM-11 with the MMU extension (Level 2: BOARD=fm11 LEVEL=2).  Sizes of the slots, see memmap.mk.
#
# The idle task and the servers (the tasks 0-5) see the memory as in Level 1: the logical pages 0-15 of 4KB are the physical pages 0-15.  A program has its
# own map: the pages 0 and 1 are the kernel (code, data, the ring of the console) and the pages 2-14 ($2000-$EFFF, 52KB) are physical pages that MM
# gives to it (the 24 pages of 4KB that the system does not use: 5-12, 16-28 of the 128KB; 29-31 are the track buffer of the disk server).
#
#   $0100-$010F  interrupt vector jump table ($FFF2-$FFFD are RAM: the kernel sets them)
#   $0600-       kernel code, then its data (K_DATA: the table of the tasks, the stack of the kernel, the buffer of xcopy)
#   $1F00-$1F81  the ring of the console (filled by the keyboard interrupt of the kernel, read by TTY)
#   ...          code of TTY, DISK, FS (the slots follow)
#   DATA_BASE-   data and stacks of the servers, the code of PM and MM
# All numbers are hexadecimal without prefix.

VEC_BASE   = 0100
SYS_BASE   = 0600
DATA_BASE  = D000
TPA_END    = D000
TOP        = FC00
KNEAR      = 1
BOUNCE     = 1D

K_CSZ      = 0C00
TTY_CSZ    = 0B80
DISK_CSZ   = 0A00
FS_CSZ     = 2D70
MM_CSZ     = 0360
PM_CSZ     = 0BE0

K_DSZ      = 0700
TTY_DSZ    = 0230
DISK_DSZ   = 01F0
FS_DSZ     = 08C0
MM_DSZ     = 0100
PM_DSZ     = 0240

HIGH       = PM MM

LOWDATA    = DISK
