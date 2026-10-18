#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""growdisk.py DISK SECTORS [FIRST LAST]  - make an OS-9 disk image larger (DD.TOT and the allocation map); the new sectors are free.
With FIRST LAST the sectors are marked used (the room of the Level 2 ROM image of the board, 720-824: tools/mkl2disk.py
writes it there, no file may be in it).  The kernel image at the end of OS9.dsk (LSN 720-735, tools/mkkernel.py) stays where it is."""
import sys
SEC = 256
if len(sys.argv) not in (3, 5): sys.exit(__doc__)
path, size = sys.argv[1], int(sys.argv[2])
d = bytearray(open(path, 'rb').read())
tot = int.from_bytes(d[0:3], 'big')
if size > tot:
    d += bytes((size - tot) * SEC)
    old_map = int.from_bytes(d[4:6], 'big')
    d[0:3] = size.to_bytes(3, 'big')
    d[4:6] = (size // 8).to_bytes(2, 'big')
    d[SEC + old_map:SEC + size // 8] = bytes(size // 8 - old_map)     # the rest of the old map sector was $FF
if len(sys.argv) == 5:
    for lsn in range(int(sys.argv[3]), int(sys.argv[4]) + 1):
        d[SEC + lsn // 8] |= 0x80 >> (lsn % 8)
open(path, 'wb').write(d)
print('%s: %d sectors' % (path, max(size, tot)))
