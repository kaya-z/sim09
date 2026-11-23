#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of MK09 2.0 on the sbc09 machine with the MMU (sim09 -2): run `make BOARD=sbc09 LEVEL=2` in mk09/ first.   Run:  tests/mk09_l2_test.py [-v]

Stage 3 of docs/V2_PLAN.md, step by step: 3b the system boots (kernel, TTY, DISK, FS, PM, MM), 3c the programs run, 3d the whole system.
"""
import os, re, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
SIM = os.path.join(os.path.dirname(MK), 'sim09')
BUILD = os.path.join(MK, 'build-sbc09-l2')
ROM = os.path.join(BUILD, 'bootrom.bin')
IMG = os.path.join(BUILD, 'mk09.img')
VERBOSE = '-v' in sys.argv
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-1500:])


if not (os.path.exists(ROM) and os.path.exists(IMG) and os.path.exists(SIM)):
    print('SKIP: run make BOARD=sbc09 LEVEL=2 in mk09/ (and make in the top directory for sim09) first')
    sys.exit(0)
work = tempfile.mkdtemp(prefix='mk09l2_')


def run(steps, timeout=300, stimeout=60000):
    script = os.path.join(work, 's.txt')
    with open(script, 'w', encoding='utf-8') as f:
        f.write('timeout %d\n' % stimeout)
        f.write('\n'.join(steps) + '\n')
    p = subprocess.run([SIM, '-2', '-max', '-rom', ROM, '-0', IMG, '-script', script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=timeout, env=dict(os.environ, LC_ALL='C'))
    out = p.stdout.decode('utf-8', 'replace').replace('\r', '')
    if VERBOSE:
        print(out)
    return out


# 3b: the boot ROM image sets the system map, the kernel starts the tasks (the idle task, TTY, DISK, FS, PM, MM) in the MMU machine, FS mounts the volume
out = run(['expect "no sh.mx"'])      # (until 3c: PM tries to start the login and cannot load a program yet)
check(re.search(r'MK09 2\.0\.0pre\d+: kernel 2\.0\.0pre\d+ TTY DISK FS PM MM', out) is not None, 'the system starts on the machine with the MMU (banner of PM)', out[:300])
check('no FAT16 volume' not in out, 'FS mounts the volume through the DMA of the disk', out[:300])
print('%d checks, %d failed' % (total, fails))
sys.exit(1 if fails else 0)
