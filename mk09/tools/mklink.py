#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mklink.py - write an lwlink script for an MK09 module.

    mklink.py CODE_BASE [DATA_BASE [START_BASE]] > module.link

The code (start, code, rodata, initgl) goes to CODE_BASE; the initialised data and the bss go to
DATA_BASE when given (system modules keep their data above the I/O page), else they follow the code.
START_BASE: the section 'start' (code that runs once, at the boot: the kernel's) goes there instead of in front of the code: the transient area, which
the programs have after the boot.  The output is meant for  lwlink --format=srec.  Entry point: _start."""
import sys

code = int(sys.argv[1], 16)
data = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
start = int(sys.argv[3], 16) if len(sys.argv) > 3 else None
print('define basesympat s_%s')
print('define lensympat l_%s')
if start is None:
    print('section start load %x' % code)
    codesecs = ('code', 'rodata', 'initgl_start', 'initgl', 'initgl_end')
else:
    print('section start load %x' % start)
    print('section code load %x' % code)
    codesecs = ('rodata', 'initgl_start', 'initgl', 'initgl_end')
for s in codesecs:
    print('section ' + s)
print('section rwdata' + (' load %x' % data if data is not None else ''))
print('section bss,bss')
print('entry _start')
