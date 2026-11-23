# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# memmap_sbc09_l2.mk - the memory map of the sbc09 machine with the MMU (BOARD=sbc09 LEVEL=2, docs/V2_PLAN.md stage 3).  Sizes of the slots, see memmap.mk.
#
# The idle task and the servers (the tasks 0-5) run in the system map: the logical blocks 0-6 of 8KB ($0000-$DFFF) are the physical blocks 0-6.  A program has
# its own map (the user map): the block 0 is the kernel (code, data, the ring of the console), the blocks 1-6 ($2000-$DFFF, 48KB) are physical blocks that MM
# gives to it.  The block 7 ($E000-$FDFF) is the window of the kernel; $FE00-$FFFF is the fixed page (the vectors and the I/O registers at $FF80).
#
#   $0100-$010F  interrupt vector jump table (the vectors of the boot ROM image point here)
#   $0600-       kernel code, then its data (K_DATA: the table of the tasks, the stack of the kernel)
#   $1F00-$1F81  the ring of the console (filled by the receive interrupt of the kernel, read by TTY)
#   ...          code of TTY, DISK, FS (the slots follow)
#   DATA_BASE-   data and stacks of the servers, the code of PM and MM
# All numbers are hexadecimal without prefix.

VEC_BASE   = 0100
SYS_BASE   = 0600
DATA_BASE  = 7000
TPA_END    = 7000
TOP        = E000
KNEAR      = 1

K_CSZ      = 0C00
TTY_CSZ    = 0C00
DISK_CSZ   = 0A00
FS_CSZ     = 3700
MM_CSZ     = 0400
PM_CSZ     = 0F00

K_DSZ      = 0700
TTY_DSZ    = 0230
DISK_DSZ   = 01F0
FS_DSZ     = 0940
MM_DSZ     = 0100
PM_DSZ     = 0280

HIGH       = PM MM

LOWDATA    = DISK
