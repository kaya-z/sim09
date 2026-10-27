#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of the code generator: every tests/cc/t_*.c is compiled with the PC builds of cpp, cc1 and cg (-DCC_HOST), assembled by `as` and run
on MK09 in sim09; the output must be the output of the IR run by tools/irsim.py.   Run:  tests/cc_sim_test.py [-v] [name...]
(Build the system first: make.)"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
ROOT = os.path.dirname(MK)
sys.path.insert(0, os.path.join(MK, 'tools'))
sys.path.insert(0, HERE)
import irsim

VERBOSE = '-v' in sys.argv
names = [a for a in sys.argv[1:] if not a.startswith('-')]
BUILD = os.path.join(MK, os.environ.get('MK09_BUILD', 'build'))
SIM = os.path.join(ROOT, 'sim09')
work = tempfile.mkdtemp(prefix='ccsim_')
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('PASS ' if cond else 'FAIL ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-2500:])


def build(src, exe):
    subprocess.run(['gcc', '-DCC_HOST', '-O1', '-w', '-I' + os.path.join(MK, 'apps'), '-o', exe, os.path.join(MK, 'apps', src)], check=True)


def compile_c(path, base, tag):
    """C file -> assembler text (PC tools); returns the text or None"""
    i_f, ir_f, s_f = [os.path.join(work, base + e) for e in ('.i', '.ir', '.s')]
    for cmd in ([os.path.join(work, 'cpp'), '-I' + os.path.join(MK, 'disk', 'inc'), path, '-o', i_f], [os.path.join(work, 'cc1'), i_f, '-o', ir_f], [os.path.join(work, 'cg'), '-p' + tag, ir_f, '-o', s_f]):
        p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=work)
        if p.returncode:
            check(False, base + ': ' + os.path.basename(cmd[0]), p.stdout.decode())
            return None, None
    return open(s_f).read(), open(ir_f).read()


for t in ('cpp', 'cc1', 'cg'):
    build(t + '.c', os.path.join(work, t))
cc = os.path.join(MK, 'lib', 'cc')
lib_s, _ = compile_c(os.path.join(cc, 'clib.c'), 'clib', 'l')
if lib_s is None:
    sys.exit(1)
tests = sorted(f for f in os.listdir(os.path.join(HERE, 'cc')) if f[:2] in ('t_', 'l_') and f.endswith('.c'))
tests = [t for t in tests if not names or t[:-2] in names]
tree = os.path.join(work, 'tree')
shutil.copytree(os.path.join(BUILD, 'tree'), tree, ignore=shutil.ignore_patterns('skk'))
want = {}
for t in tests:
    base = t[:-2]
    s, ir = compile_c(os.path.join(HERE, 'cc', t), base, 'p')
    if s is None:
        continue
    if t.startswith('l_'):         # the library tests: the output of gcc
        exe = os.path.join(work, base + '.gcc')
        subprocess.run(['gcc', '-w', '-fwrapv', '-o', exe, os.path.join(HERE, 'cc', t)], check=True)
        out = subprocess.run([exe], stdout=subprocess.PIPE).stdout
    else:
        out, code = irsim.run(ir)
    want[base] = out
    allsrc = open(os.path.join(cc, 'start.s')).read() + s + lib_s + open(os.path.join(cc, 'rt.s')).read()
    open(os.path.join(tree, base.upper()[:8] + '.S'), 'w').write(allsrc)
disk = os.path.join(work, 'disk.img')
subprocess.run([sys.executable, '-X', 'utf8', os.path.join(MK, 'tools', 'mkdisk.py'), '-o', disk, '--boot', os.path.join(BUILD, 'boot.img'),
                '--size-mb', '16', tree], check=True, stdout=subprocess.DEVNULL)
steps = ['timeout 3000000', 'expect "type help"', 'expect "/> "']
for base in want:
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
for base in want:
    m = re.search(r'echo ==%s==\n==%s==\n/> /%s.mx\n(.*?)/> ' % (base, base, base[:8].upper()), out, re.S)
    got = m.group(1) if m else ''
    asm_err = re.search(r'as /%s.s\n(.*?)/> ' % base[:8].upper(), out, re.S)
    check(got.encode('latin-1') == want[base], base + ': the program compiled by cc runs on MK09 and gives the same output',
          'as: %s\ngot:\n%s\nwant:\n%s' % (asm_err.group(1) if asm_err else '?', got, want[base].decode('latin-1')))
print('%d checks, %d failed' % (total, fails))
shutil.rmtree(work, ignore_errors=True)
sys.exit(1 if fails else 0)
