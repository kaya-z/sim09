#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkmx.py - make a relocatable MX executable from two images of the same program.

    mkmx.py image_at_B image_at_B+256 -o program.mx

Both images are the raw output of lwlink for the same objects, linked at two addresses 256 bytes apart.
Every byte that differs must be the high byte of an address (it differs by exactly 1); the positions of
these bytes make the relocation table that is put behind the first image (format: lib/crt0app.s).
The header (the first 16 bytes) is the one of the first image, with the size of the image filled in
at +8; its load and entry addresses must differ by 256."""
import argparse, sys

ap = argparse.ArgumentParser()
ap.add_argument('image1')
ap.add_argument('image2')
ap.add_argument('-o', '--output', required=True)
a = ap.parse_args()
x = bytearray(open(a.image1, 'rb').read())
y = open(a.image2, 'rb').read()
if len(x) != len(y) or len(x) < 16:
    sys.exit('mkmx: the two images differ in length (%d, %d)' % (len(x), len(y)))
if x[0:4] != y[0:4] or x[8:16] != y[8:16]:
    sys.exit('mkmx: the headers differ in more than the addresses')
for off in (4, 6):
    if ((y[off] << 8 | y[off + 1]) - (x[off] << 8 | x[off + 1])) != 256:
        sys.exit('mkmx: the address at +%d of the header does not differ by 256' % off)
places = []
for i in range(16, len(x)):
    if x[i] != y[i]:
        if (y[i] - x[i]) & 0xFF != 1:
            sys.exit('mkmx: byte %d differs by %d: it is not the high byte of an address' % (i, (y[i] - x[i]) & 0xFF))
        places.append(i)
table = bytearray()
cur = 0
for p in places:
    g = p - cur
    while g > 254:
        table.append(255)
        g -= 254
    table.append(g)
    cur = p
table.append(0)
size = len(x)
x[8] = size >> 8
x[9] = size & 0xFF
open(a.output, 'wb').write(bytes(x) + bytes(table))
print('%s: image %d bytes, %d relocations, table %d bytes' % (a.output, size, len(places), len(table)))
