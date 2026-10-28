#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of the Pascal compiler: every tests/pas/p_*.pas is compiled with the PC builds of pas1 and cg (-DCC_HOST), assembled by `as` and run on MK09 in
sim09; the output must be the contents of p_*.out.   Run:  tests/pas_test.py [-v] [name...]   (Build the system first: make.)"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
VERBOSE = '-v' in sys.argv
names = [a for a in sys.argv[1:] if not a.startswith('-')]
BUILD = os.path.join(MK, os.environ.get('MK09_BUILD', 'build'))
SIM = os.path.join(ROOT, 'sim09')
work = tempfile.mkdtemp(prefix='pastest_')
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-2500:])


for t in ('pas0', 'pas1', 'cg'):
    subprocess.run(['gcc', '-DCC_HOST', '-O1', '-w', '-I' + os.path.join(MK, 'apps'), '-o', os.path.join(work, t), os.path.join(MK, 'apps', t + '.c')], check=True)
libdir = os.path.join(work, 'lib')
os.makedirs(libdir)
for f in ('clib', 'rt', 'start'):
    shutil.copy(os.path.join(BUILD, 'tree', 'lib', f + '.s'), os.path.join(libdir, f.upper() + '.S'))
tests = sorted(f for f in os.listdir(os.path.join(HERE, 'pas')) if f.startswith('p_') and f.endswith('.pas'))
tests = [t for t in tests if not names or t[:-4] in names]
tree = os.path.join(work, 'tree')
shutil.copytree(os.path.join(BUILD, 'tree'), tree, ignore=shutil.ignore_patterns('skk'))
ok = []
for t in tests:
    base = t[:-4]
    src = os.path.join(work, base + '.pas')
    shutil.copy(os.path.join(HERE, 'pas', t), src)
    p = subprocess.run([os.path.join(work, 'pas0'), '-o', os.path.join(work, base + '.tok'), src], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=work)
    if p.returncode:
        check(False, base + ': pas0', p.stdout.decode())
        continue
    p = subprocess.run([os.path.join(work, 'pas1'), '-o', os.path.join(work, base + '.ir'), os.path.join(work, base + '.tok')], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=work)
    if p.returncode:
        check(False, base + ': pas1', p.stdout.decode())
        continue
    a_f = os.path.join(work, base + '.A')
    p = subprocess.run([os.path.join(work, 'cg'), '-pp', '-L', os.path.join(work, base + '.ir'), '-o', a_f], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       cwd=work, env=dict(os.environ, CC_LIB=libdir))
    if p.returncode:
        check(False, base + ': cg -L', p.stdout.decode())
        continue
    shutil.copy(a_f, os.path.join(tree, base.upper()[:8] + '.S'))
    ok.append(base)
disk = os.path.join(work, 'disk.img')
subprocess.run([sys.executable, '-X', 'utf8', os.path.join(MK, 'tools', 'mkdisk.py'), '-o', disk, '--boot', os.path.join(BUILD, 'boot.img'),
                '--size-mb', '16', tree], check=True, stdout=subprocess.DEVNULL)
steps = ['timeout 3000000', 'expect "type help"', 'expect "/> "']
for base in ok:
    n = base.upper()[:8]
    steps += ['send "as /%s.s\\r"' % n, 'expect "/> "', 'send "echo ==%s==\\r"' % base, 'expect "/> "', 'send "/%s.mx\\r"' % n, 'expect "/> "']
steps += ['send "shutdown\\r"', 'sleep 300']
script = os.path.join(work, 's.txt')
open(script, 'w').write('\n'.join(steps) + '\n')
p = subprocess.run([SIM, '-max', '-rom', os.path.join(BUILD, 'bootrom.bin'), '-0', disk, '-script', script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                   timeout=3000, env=dict(os.environ, LC_ALL='C'))
out = p.stdout.decode('latin-1').replace('\r', '')
if VERBOSE:
    print(out)
for base in ok:
    want = open(os.path.join(HERE, 'pas', base + '.out'), encoding='latin-1').read()
    m = re.search(r'echo ==%s==\n==%s==\n/> /%s.mx\n(.*?)/> ' % (base, base, base[:8].upper()), out, re.S)
    got = m.group(1) if m else ''
    asm_err = re.search(r'as /%s.s\n(.*?)/> ' % base[:8].upper(), out, re.S)
    check(got == want, base + ': the Pascal program runs on MK09 and gives the output',
          'as: %s\ngot:\n%s\nwant:\n%s' % (asm_err.group(1) if asm_err else '?', got, want))
print('%d checks, %d failed' % (total, fails))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if fails else 0)
