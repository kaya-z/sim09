#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""fitapps.py - which programs fit the memory of the machine.

    fitapps.py PAGES  prog1.mx prog2.mx ...        prints the names (without .mx) of the programs that can run, one per line

PM gives a program  image + max(bss, relocation table) + stack + 128  bytes, rounded up to pages of 256 bytes (docs/SPEC.md 9.1); a program that needs
more than PAGES pages gets E_NOMEM and cannot run.  A program that needs one that cannot run (cc: cc1 and cg, pc: pas1) is left out too.
The programs that are left out are listed on the standard error."""
import os, struct, sys

DEPENDS = {'cc': ['cc1', 'cg'], 'pc': ['pas1']}

pages = int(sys.argv[1])
need = {}
for fn in sys.argv[2:]:
    d = open(fn, 'rb').read()
    size, bss, stack = struct.unpack('>HHH', d[8:14])
    extra = len(d) - size
    need[os.path.basename(fn)[:-3]] = (size + max(bss, extra) + stack + 128 + 255) // 256
ok = {n for n, p in need.items() if p <= pages}
for n, deps in DEPENDS.items():
    if n in ok and any(x in need and x not in ok for x in deps):
        ok.discard(n)
for n in sorted(need):
    if n in ok:
        print(n)
    else:
        sys.stderr.write('not in the image: %s (%d pages%s)\n' % (n, need[n], '' if need[n] > pages else ', needs a program that does not fit'))
