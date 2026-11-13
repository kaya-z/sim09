#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mk2hd.py - make a raw 2HD floppy disk image of the FM-11 from an MK09 disk image.

    mk2hd.py -o mk09_2hd.img --ipl ipl_2hd.bin disk.img

The raw image is the "FDD_2HD_256" layout of the FM-11 simulators (1021696 bytes): track 0 side 0 has 26 sectors of 128 bytes
(3328 bytes), everything behind it 256 byte sectors (76 x 2 x 26 + 26 = 3978).  The IPL (at most 512 bytes: the boot ROM reads the
sectors 1-4 of track 0 side 0) goes to the first bytes; the MK09 disk (3978 sectors) follows, sector for sector."""
import argparse, sys

ap = argparse.ArgumentParser()
ap.add_argument('-o', '--output', required=True)
ap.add_argument('--ipl', required=True)
ap.add_argument('disk')
a = ap.parse_args()

TRACK0 = 26 * 128
SECTORS = 3978
ipl = open(a.ipl, 'rb').read()
disk = open(a.disk, 'rb').read()
if len(ipl) > 4 * 128:
    sys.exit('mk2hd: the IPL (%d bytes) does not fit in the 4 sectors that the boot ROM reads' % len(ipl))
if len(disk) > SECTORS * 256:
    sys.exit('mk2hd: the disk image (%d bytes) is bigger than %d sectors' % (len(disk), SECTORS))
img = ipl.ljust(TRACK0, b'\0') + disk.ljust(SECTORS * 256, b'\0')
open(a.output, 'wb').write(img)
print('2HD image %s: %d bytes' % (a.output, len(img)))
