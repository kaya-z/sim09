# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
# memmap_fm11.mk - the memory map of the FM-11 (BOARD=fm11): the SIZES of the slots, see memmap.mk for the method.
#
#   $0000-$00FF  page 0 (not used)
#   $0100-$010F  interrupt vector jump table ($FFF2-$FFFD, RAM on the FM-11, are set by the kernel)
#   $0110-$01FF  data of some system modules (LOWDATA)
#   $0200-$02FF  sector buffer of the IPL (free after the boot)
#   $0400-$05FF  the IPL itself: the boot ROM of the FM-11 loads it here (free after the boot, not used by the system)
#   $0600-       system code: kernel, TTY, DISK, FS, PM (one slot each, read from the boot image)
#   TPA_BASE-    transient program area (programs, handed out in pages of 256 bytes by the MM server)
#   $E100-$FBFF  data, bss and stacks of the system modules and the high code
#   $FC00-$FFFF  hardware: RAM $FC00-$FC7F, shared memory with the sub CPU, the I/O page $FD00-$FDFF, RAM and the vectors
# All numbers are hexadecimal without prefix.

VEC_BASE   = 0100
SYS_BASE   = 0600
DATA_BASE  = DFC0
TPA_END    = DFC0
TOP        = FC00

# size of the code slots (the build checks that the code fits)
K_CSZ      = 07E0
TTY_CSZ    = 0B50
DISK_CSZ   = 0860
FS_CSZ     = 2C90
MM_CSZ     = 01C4
PM_CSZ     = 0B36

# size of the data slots (rwdata + bss + stack)
K_DSZ      = 0264
TTY_DSZ    = 0230
DISK_DSZ   = 01F0
FS_DSZ     = 08C0
MM_DSZ     = 0060
PM_DSZ     = 0190

HIGH       = PM MM

LOWDATA    = DISK MM
