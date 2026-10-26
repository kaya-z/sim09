#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of the C compiler on the PC (docs/IR.md): cpp and cc1 are built with gcc (-DCC_HOST), every tests/cc/t_*.c is compiled to IR and run with
tools/irsim.py, and the output is compared with what gcc makes of the same program (or with the file t_*.out when there is one).
Run:  tests/cc_test.py [-v] [name...]"""
import os, subprocess, sys, tempfile, shutil

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(MK, 'tools'))
import irsim

VERBOSE = '-v' in sys.argv
names = [a for a in sys.argv[1:] if not a.startswith('-')]
work = tempfile.mkdtemp(prefix='cctest_')
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-1500:])


def build(src, exe):
    subprocess.run(['gcc', '-DCC_HOST', '-O1', '-w', '-I' + os.path.join(MK, 'apps'), '-o', exe, os.path.join(MK, 'apps', src)], check=True)


build('cpp.c', os.path.join(work, 'cpp'))
build('cc1.c', os.path.join(work, 'cc1'))
tests = sorted(f for f in os.listdir(os.path.join(HERE, 'cc')) if f.startswith('t_') and f.endswith('.c'))
for t in tests:
    base = t[:-2]
    if names and base not in names:
        continue
    src = os.path.join(HERE, 'cc', t)
    i_file = os.path.join(work, base + '.i')
    ir_file = os.path.join(work, base + '.ir')
    p = subprocess.run([os.path.join(work, 'cpp'), src, '-o', i_file], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=work)
    check(p.returncode == 0, base + ': cpp', p.stdout.decode())
    if p.returncode:
        continue
    p = subprocess.run([os.path.join(work, 'cc1'), i_file, '-o', ir_file], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=work)
    check(p.returncode == 0, base + ': cc1', p.stdout.decode())
    if p.returncode:
        continue
    if VERBOSE:
        print(open(ir_file).read())
    try:
        out, code = irsim.run(open(ir_file).read())
    except Exception as e:
        check(False, base + ': irsim', repr(e))
        continue
    want_file = os.path.join(HERE, 'cc', base + '.out')
    if os.path.exists(want_file):
        want = open(want_file, 'rb').read()
    else:
        exe = os.path.join(work, base + '.gcc')
        g = subprocess.run(['gcc', '-w', '-fwrapv', '-o', exe, src], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if g.returncode:
            check(False, base + ': gcc', g.stdout.decode())
            continue
        want = subprocess.run([exe], stdout=subprocess.PIPE).stdout
    check(out == want, base + ': the IR gives the output of the program', 'got:\n%s\nwant:\n%s' % (out.decode('latin-1'), want.decode('latin-1')))
print('%d checks, %d failed' % (total, fails))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if fails else 0)
