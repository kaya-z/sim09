#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""addbasic09.py [-2] IN.dsk OUT.dsk  - OUT.dsk is IN.dsk with BASIC09 and RunB on it (/d0/cmds/basic09, /d0/cmds/runb).

BASIC09 (Microware, Motorola) is not in the repository: build the modules first with  make -C os9/basic09  (it fetches the
sources, os9/basic09/fetch.sh; needs lwtools and python3).  With -2 the disk is the one of Level 2 (OS9L2.dsk).  The copy is done
by sim09 itself (copy from /v0 to /d0, attr), so sim09 has to be built.

  tools/addbasic09.py os9/OS9.dsk os9/OS9-basic09.dsk          Level 1 (and the disk of the board for Level 1)
  tools/addbasic09.py -2 os9/OS9L2.dsk os9/OS9L2-basic09.dsk   Level 2"""
import os, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
args = sys.argv[1:]
l2 = bool(args) and args[0] == '-2'
if l2: args = args[1:]
if len(args) != 2: sys.exit(__doc__)
src, out = args
mods = [os.path.join(ROOT, 'os9', 'basic09', n) for n in ('basic09', 'runb')]
for m in mods:
    if not os.path.isfile(m): sys.exit('addbasic09: %s is missing: make -C os9/basic09' % m)
shutil.copyfile(src, out)
d = tempfile.mkdtemp(prefix='b09_')
os.mkdir(os.path.join(d, 'cmds'))
for m in mods: shutil.copy(m, os.path.join(d, 'cmds'))
lines = ['timeout 3000000', 'expect "OS9:"', 'send "chx /d0/cmds\\r"', 'expect "OS9:"', 'send "del /d0/cmds/RUNB\\r"', 'expect "OS9:"']
for n in ('basic09', 'runb'):
    lines += ['send "del /d0/cmds/%s\\r"' % n, 'expect "OS9:"', 'send "copy /v0/cmds/%s /d0/cmds/%s\\r"' % (n, n), 'expect "OS9:"',
              'send "attr /d0/cmds/%s e pe r pr\\r"' % n, 'expect "OS9:"']
lines += ['send "free /d0\\r"', 'expect "OS9:"', 'quit']
sp = os.path.join(d, 's.txt')
open(sp, 'w').write('\n'.join(lines) + '\n')
level = 'os9v2.rom' if l2 else 'os9v1.rom'
r = subprocess.run([os.path.join(ROOT, 'sim09')] + (['-2'] if l2 else []) + ['-max', '-rom', os.path.join(ROOT, 'os9', level), '-0', out, '-v', d, '-script', sp],
                   capture_output=True, env=dict(os.environ, LC_ALL='C'))
txt = r.stdout.decode('latin1').replace('\r', '')
shutil.rmtree(d, ignore_errors=True)
if 'copy:' in txt or 'ERROR #2' in txt.split('copy /v0')[-1]:
    sys.exit('addbasic09: the copy failed:\n' + txt[-400:])
free = [l for l in txt.split('\n') if 'Free sectors' in l]
print('%s: BASIC09 and RunB added; %s' % (out, free[-1].strip() if free else ''))
