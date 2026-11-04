#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of the OS-9 build of the Pascal compiler, the part that makes the programs: every tests/pas/p_*.pas is compiled with the PC builds of pas0, pas1 and
cg (-DCC_HOST), assembled by the PC build of `as -9` into an OS-9 module with the OS-9 library (clib.c -DOS9, rt.s + rt9.s) and run on NitrOS-9 Level 2 in
sim09; the output must be the contents of p_*.out (CR LF = LF).   Run:  tests/pas9_test.py [-v] [name...]"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
VERBOSE = '-v' in sys.argv
names = [a for a in sys.argv[1:] if not a.startswith('-')]
SIM = os.path.join(ROOT, 'sim09')
work = tempfile.mkdtemp(prefix='pas9test_')
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-2500:])


def gcc(defs, src, out):
    subprocess.run(['gcc'] + defs + ['-O1', '-w', '-I' + os.path.join(MK, 'apps'), '-o', os.path.join(work, out), os.path.join(MK, 'apps', src)], check=True)


for t in ('pas0', 'pas1', 'cg', 'cpp', 'cc1'):
    gcc(['-DCC_HOST'], t + '.c', t)
gcc(['-DAS_HOST', '-DAS_OBJ'], 'as.c', 'as_obj')
gcc(['-DAS_HOST', '-DOS9'], 'as.c', 'as')       # (-DOS9: the tables of the OS-9 build, so that both builds accept the same programs)

# the library of the OS-9 build
lib = os.path.join(work, 'lib')
os.makedirs(lib)
def run(cmd, **kw):
    p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=kw.get('cwd', work), env=dict(os.environ, **kw.get('env', {})))
    return p.returncode, p.stdout.decode('latin-1')
rc, o = run(['./cpp', '-DOS9', os.path.join(MK, 'lib', 'cc', 'clib.c'), '-o', 'clib9.i'])
rc2, o2 = run(['./cc1', 'clib9.i', '-o', 'clib9.ir'])
rc3, o3 = run(['./cg', '-pl', 'clib9.ir', '-o', 'clib9.s'])
check(rc == rc2 == rc3 == 0, 'the C library for OS-9 compiles', o + o2 + o3)
open(os.path.join(work, 'rt9.s'), 'wb').write(open(os.path.join(MK, 'lib', 'cc', 'rt.s'), 'rb').read() + open(os.path.join(MK, 'lib', 'cc', 'rt9.s'), 'rb').read())
for src, name in (('clib9.s', 'CLIB'), ('rt9.s', 'RT')):
    rc, o = run([sys.executable, os.path.join(MK, 'tools', 'mkobj.py'), './as_obj', src, os.path.join(lib, name + '.OBJ'), os.path.join(lib, name + '.IDX')])
    check(rc == 0, name + ' library for OS-9: the blocks are assembled', o)
shutil.copy(os.path.join(MK, 'lib', 'cc', 'start.s'), os.path.join(lib, 'START.S'))

pas = os.path.join(HERE, 'pas')
for f in os.listdir(pas):          # (units and include files that the programs read)
    if (f.endswith('.pas') or f.endswith('.inc')) and not f.startswith('p_'):
        shutil.copy(os.path.join(pas, f), os.path.join(work, f))
tests = sorted(f[:-4] for f in os.listdir(pas) if f.startswith('p_') and f.endswith('.pas'))
tests += sorted('d_' + f[:-4] for f in os.listdir(os.path.join(MK, 'disk', 'pas')) if f.endswith('.pas') and os.path.exists(os.path.join(pas, 'd_' + f[:-4] + '.out')))
tests = [t for t in tests if not names or t in names]
v0 = os.path.join(work, 'v0')
os.makedirs(os.path.join(v0, 'cmds'))
for f in os.listdir(os.path.join(ROOT, 'os9', 'level2', 'cmds')):
    shutil.copy(os.path.join(ROOT, 'os9', 'level2', 'cmds', f), os.path.join(v0, 'cmds', f))
ok = []
for base in tests:
    src = os.path.join(work, base + '.pas')
    text = open(os.path.join(MK, 'disk', 'pas', base[2:] + '.pas') if base.startswith('d_') else os.path.join(pas, base + '.pas'), encoding='latin-1').read()
    text = re.sub(r"'/(pas\w+\.\w+)'", r"'\1'", text)          # (the files are in the current directory: MK09 has a root directory, OS-9 has devices)
    open(src, 'w', encoding='latin-1').write(text)
    steps = [['./pas0', '-o', base + '.tok', base + '.pas'], ['./pas1', '-o', base + '.ir', base + '.tok'], ['./cg', '-pp', '-L', base + '.ir', '-o', base + '.A'],
             ['./as', '-9', '-o', os.path.join(v0, 'cmds', base), base + '.A']]
    good = True
    for st in steps:
        rc, o = run(st, env={'CC_LIB': lib, 'AS_LIB': lib})
        if rc:
            check(False, base + ': ' + os.path.basename(st[0]), o)
            good = False
            break
    if good:
        ok.append(base)
disk = os.path.join(work, 'OS9L2.dsk')
shutil.copy(os.path.join(ROOT, 'os9', 'OS9L2.dsk'), disk)
steps = ['timeout 60000', 'expect "OS9:"', 'send "chx /v0/cmds\\r"', 'expect "OS9:"', 'send "chd /v0\\r"', 'expect "OS9:"']
for base in ok:
    steps += ['send "echo ==%s==\\r"' % base, 'expect "OS9:"', 'send "%s\\r"' % base, 'expect "OS9:"']
steps += ['quit']
script = os.path.join(work, 's.txt')
open(script, 'w').write('\n'.join(steps) + '\n')
p = subprocess.run([SIM, '-2', '-max', '-rom', os.path.join(ROOT, 'os9', 'os9v2.rom'), '-0', disk, '-v', v0, '-script', script],
                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=3000, env=dict(os.environ, LC_ALL='C'))
out = p.stdout.decode('latin-1').replace('\r\n', '\n').replace('\r', '\n')
if VERBOSE:
    print(out)
for base in ok:
    want = open(os.path.join(pas, base + '.out'), encoding='latin-1').read()
    m = re.search(r'OS9:echo ==%s==\n==%s==\n\nOS9:%s\n(.*?)\nOS9:' % (base, base, base), out, re.S)
    got = (m.group(1) + '\n') if m else ''
    # (the shell prints an empty line before the prompt)
    check(got == want or got.rstrip('\n') == want.rstrip('\n'), base + ': the Pascal program runs on OS-9 Level 2 and gives the output', 'got:\n%s\nwant:\n%s' % (got, want))

# ---- the compiler itself on OS-9: every program is compiled by `pc` in the simulator (pas0, pas1, cg, as of os9/pascal on the disk os9/OS9L2.dsk, the library in /d0/LIB);
# the module must be the same, byte for byte, as the one that the PC builds made, and must give the output
if ok:
    nat = os.path.join(work, 'native')
    os.makedirs(nat)
    for base in ok:
        shutil.copy(os.path.join(work, base + '.pas'), os.path.join(nat, base + '.pas'))
    for f in os.listdir(work):
        if (f.endswith('.pas') or f.endswith('.inc')) and not f.startswith(('p_', 'd_')):
            shutil.copy(os.path.join(work, f), os.path.join(nat, f))
    disk2 = os.path.join(work, 'OS9L2n.dsk')
    shutil.copy(os.path.join(ROOT, 'os9', 'OS9L2.dsk'), disk2)
    steps = ['timeout 600000', 'expect "OS9:"', 'send "chx /d0/cmds\\r"', 'expect "OS9:"', 'send "chd /v0\\r"', 'expect "OS9:"']
    for base in ok:
        steps += ['send "echo ==%s==\\r"' % base, 'expect "OS9:"', 'send "pc %s.pas\\r"' % base, 'expect "OS9:"']
    steps += ['send "dir\\r"', 'expect "OS9:"', 'send "chx /v0\\r"', 'expect "OS9:"']
    for base in ok:
        steps += ['send "/d0/cmds/echo ==%s==\\r"' % base, 'expect "OS9:"', 'send "%s\\r"' % base, 'expect "OS9:"']
    steps += ['quit']
    script2 = os.path.join(work, 's2.txt')
    open(script2, 'w').write('\n'.join(steps) + '\n')
    p = subprocess.run([SIM, '-2', '-max', '-rom', os.path.join(ROOT, 'os9', 'os9v2.rom'), '-0', disk2, '-v', nat, '-script', script2],
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=3000, env=dict(os.environ, LC_ALL='C'))
    out2 = p.stdout.decode('latin-1').replace('\r\n', '\n').replace('\r', '\n')
    if VERBOSE:
        print(out2)
    for base in ok:
        mod = os.path.join(nat, base)
        same = os.path.exists(mod) and open(mod, 'rb').read() == open(os.path.join(v0, 'cmds', base), 'rb').read()
        check(same, base + ': the module made by pc on OS-9 is the same as the one made by the PC builds', '')
        want = open(os.path.join(pas, base + '.out'), encoding='latin-1').read()
        m = re.findall(r'OS9:/d0/cmds/echo ==%s==\n==%s==\n\nOS9:%s\n(.*?)\nOS9:' % (base, base, base), out2, re.S)
        got = (m[-1] + '\n') if m else ''
        check(got == want or got.rstrip('\n') == want.rstrip('\n'), base + ': the program compiled on OS-9 runs and gives the output', 'got:\n%s\nwant:\n%s' % (got, want))

print('%d checks, %d failed' % (total, fails))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if fails else 0)
