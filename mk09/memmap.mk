# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# memmap.mk - the memory map of the sbc09 machine (64KB, flat, no MMU): the SIZES of the slots.
# tools/genmap.py lays the slots out one after the other and writes build/memmap.h (C and lwasm)
# and build/memmap.gen.mk (the addresses for this Makefile).
#
#   $0000-$00FF  page 0 (not used; a null pointer stays invalid)
#   $0100-$010F  interrupt vector jump table (the boot ROM vectors point here)
#   $0200-$02FF  sector buffer of the boot ROM (free after the boot)
#   $0300-       system code: kernel, TTY, DISK, FS, PM (one slot each, read from the boot image)
#   TPA_BASE-    transient program area: the memory that the memory manager (servers/mm.c, in PM)
#                hands out in pages of 256 bytes to the programs and to the shell (the shell is
#                taken out of memory by PM when a bigger program needs the room)
#   $E000-$E1FF  I/O page
#   $E200-$FEFF  data, bss and stacks of the system modules (one slot each)
#   $FF00-$FFFF  boot ROM and hardware vectors
# All numbers are hexadecimal without prefix.

VEC_BASE   = 0100
SYS_BASE   = 0300
DATA_BASE  = E200
TPA_END    = E000

# size of the code slots (the build checks that the code fits)
K_CSZ      = 0858
TTY_CSZ    = 08CC
DISK_CSZ   = 029C
FS_CSZ     = 27D4
MM_CSZ     = 01C4
PM_CSZ     = 0B0A

# size of the data slots (rwdata + bss + stack)
K_DSZ      = 0260
TTY_DSZ    = 01C0
DISK_DSZ   = 0190
FS_DSZ     = 0780
MM_DSZ     = 0060
PM_DSZ     = 01F0

# The memory above the I/O page has more room than the data need: the code of these modules is
# put there (behind the data slots) instead of into the low memory, which the programs need.
HIGH       = DISK PM MM

# The pages $0110-$02FF are free once the system is up (the boot ROM used $0200 for its sector buffer): the data of these modules
# (only bss and a little initialised data below $0200) goes there, which leaves room in the high memory.
LOWDATA    = DISK MM

