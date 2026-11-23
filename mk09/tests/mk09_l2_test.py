#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""Tests of MK09 2.0 on the sbc09 machine with the MMU (sim09 -2): run `make BOARD=sbc09 LEVEL=2` in mk09/ first.   Run:  tests/mk09_l2_test.py [-v]

Stage 3 of docs/V2_PLAN.md: the system starts, the login, the shell and the programs run (the 2.0 features of the stages 1 and 2 on this machine).
"""
import os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
SIM = os.path.join(os.path.dirname(MK), 'sim09')
BUILD = os.path.join(MK, 'build-sbc09-l2')
ROM = os.path.join(BUILD, 'bootrom.bin')
IMG = os.path.join(BUILD, 'mk09.img')
sys.path.insert(0, HERE)
import fatlib
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
disk = os.path.join(work, 'disk.img')
shutil.copy(IMG, disk)


def run(steps, timeout=900, stimeout=600000):
    script = os.path.join(work, 's.txt')
    with open(script, 'w', encoding='utf-8') as f:
        f.write('timeout %d\n' % stimeout)
        f.write('\n'.join(steps) + '\n')
    p = subprocess.run([SIM, '-2', '-max', '-rom', ROM, '-0', disk, '-script', script], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       timeout=timeout, env=dict(os.environ, LC_ALL='C'))
    out = p.stdout.decode('utf-8', 'replace').replace('\r', '')
    if VERBOSE:
        print(out)
    return out, p.returncode


def login(user='root', pw='root'):
    return ['expect "login: "', 'send "%s\\r"' % user, 'expect "Password: "', 'send "%s\\r"' % pw]


def sh(*lines, prompt='/> '):      # (not '> ': ls -l prints <DIR> )
    r = []
    for l in lines:
        r.append('send "%s\\r"' % l.replace('"', '\\"'))
        r.append('expect "%s"' % prompt)
    return r


ROOT = login() + ['expect "/> "']
OFF = ['send "shutdown\\r"', 'sleep 300']

# ---- 3b/3c: the system starts, the login and the shell
out, rc = run(ROOT + sh('id', 'ls -l /', 'ls /bin', 'cat /etc/rc.sh', 'ver', 'ps') + OFF)
check(re.search(r'MK09 2\.0\.0pre\d+: kernel 2\.0\.0pre\d+ TTY DISK FS PM MM', out) is not None, 'the system starts on the machine with the MMU (banner of PM)', out[:300])
check('no FAT16 volume' not in out and 'MK09 login:' in out, 'FS mounts the volume and PM starts the login')
check('MMU: 8KB pages' in out, 'the shell says that this is the machine with the MMU')
check('uid=0(root) gid=0(root)' in out, 'id: root after the login')
check(re.search(r'drwxr-xr-x root\s+root\s+bin\s', out) is not None and re.search(r'drwxrwxrwx root\s+root\s+tmp\s', out) is not None, 'ls -l shows the permissions, the owner and the group (lower case names)', out[-1500:])
check(all(n in out for n in ('sh.mx', 'ls.mx', 'login.mx')), 'ls /bin lists the programs (lower case)')
check(re.search(r'^\s*\d+ (ready|running)', out, re.M) is not None and re.search(r'^\s*7 running', out, re.M) is not None, 'ps lists the programs in the user map', out[-700:])
check(rc == 0 and out.rstrip().endswith('CPU halted.'), 'shutdown (root) powers the simulator off', out[-200:])

# ---- the names are case sensitive
out, rc = run(ROOT + sh('cp /etc/rc.sh /Aa.txt', 'cp /etc/rc.sh /aA.txt', 'ls /') + OFF)
check('Aa.txt' in out and 'aA.txt' in out, 'two files that differ only in the case live together')

# ---- the programs run in their own address spaces
out, rc = run(ROOT + sh('dhry 200') + ['send "basic\\r"', 'expect "Ok"', 'send "PRINT 6*7\\r"', 'expect "42"', 'send "SYSTEM\\r"', 'expect "/> "']
              + sh('cc asm/hello.c', 'asm/hello') + OFF)
check('Dhrystone checks OK' in out and 'WRONG' not in out, 'dhry: the final values of Dhrystone are right (a program in its own map)', out[-700:])
check(' 42' in out, 'BASIC runs')
check('hello from C' in out and re.search(r'hello.mx: \d+ bytes', out) is not None, 'cc compiles a program and it runs (the stages follow each other with PM_EXEC)', out[-500:])

# ---- the accounts: login, permissions, su, passwd, root only operations
out, rc = run(login('root', 'wrong') + ['expect "Login incorrect"'] + login('user', 'user') + ['expect "/home/user> "']
              + sh('id', 'pwd', 'mkdir /pub', 'ls -l /etc', 'date -s 2030-01-01 00:00:00', prompt='/home/user> ')
              + ['send "su\\r"', 'expect "Password: "', 'send "bad\\r"', 'expect "/home/user> "']
              + ['send "su\\r"', 'expect "Password: "', 'send "root\\r"', 'expect "/home/user> "'] + sh('id', prompt='/home/user> ') + OFF)
check('Login incorrect' in out, 'a wrong password is refused')
check('uid=100(user) gid=100(users)' in out and '/home/user>' in out, 'user logs in and starts in the home directory')
check('not allowed' in out, 'user cannot make a directory in / (root\'s, 755)')
check('rw------- root' in out, 'ls -l: shadow is 600 and root\'s')
check('cannot be set' in out, 'user cannot set the clock')
check('su: wrong password' in out and 'uid=0(root) gid=0(root)' in out.split('su: wrong password')[1], 'su: a wrong password is refused, the right one gives root', out[-900:])

out, rc = run(ROOT + sh('passwd user')[:1] + ['expect "New password: "', 'send "nu\\r"', 'expect "Again: "', 'send "nu\\r"', 'expect "/> "', 'send "exit\\r"']
              + login('user', 'nu') + ['expect "/home/user> "'] + sh('id') + OFF)
check('password changed' in out and 'uid=100(user)' in out.split('password changed')[1], 'passwd: root sets the password of user, user logs in with it', out[-900:])
# ---- 3d: the other programs and the memory
out, rc = run(ROOT + sh('mem', 'pc pas/hanoi.pas', 'pas/hanoi 3', 'fstest', 'faults', 'stk', 'echo hi | cat', 'spin &', 'ps', 'jwp -v') + ['send "reboot\\r"', 'expect "login: "'])
check('MMU' in out and 'program area' in out and re.search(r'total\s+440\.0 KB', out) is not None, 'mem shows the map of the machine with the MMU and the memory of the programs', out[-1500:])
check('pas/hanoi.mx:' in out and ' 15: disk 1 from B to C' in out, 'pc compiles Pascal and the program runs')
check('fstest: ok' in out, 'fstest: the file system tests of the program pass')
check('faults: stopped by the system (stack overflow)' in out and 'message outside my memory: 4' in out, 'faults: the kernel checks the memory of the program (the map)')
check(re.search(r'^\s+5 [0-9a-f]{4}-[0-9a-f]{4}\s+\d+', out, re.M) is not None, 'stk lists the stacks of the servers')
check('\nhi\n' in out, 'a pipe between two programs')
check(re.search(r'\[\d+\]', out) is not None and re.search(r'^\s*\d+ ready\s+4\s+2000-', out, re.M) is not None, 'a program in the background (spin &) is a task of its own, ps shows it')
check('jwp 1.0' in out, 'jwp -v')
check(len(re.findall(r'MK09 login: ', out)) >= 2 and out.count('MK09 2.0.0pre') >= 2, 'reboot writes the files and starts the system again (the login comes back)', out[-400:])
fsck = subprocess.run(['sh', '-c', 'dd if=%s of=%s/v.img bs=512 skip=2048 2>/dev/null; fsck.fat -n %s/v.img' % (disk, work, work)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
check(fsck.returncode == 0, 'fsck.fat: the volume is right after all these runs', fsck.stdout.decode('utf-8', 'replace')[-600:])
print('%d checks, %d failed' % (total, fails))
sys.exit(1 if fails else 0)
