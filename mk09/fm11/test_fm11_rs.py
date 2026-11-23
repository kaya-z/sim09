#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""FM-11 Level 2 (MK09 2.0): two terminals at the same time.  The screen and the keyboard are the first terminal, the RS-232C port (i8251 channel 0) is the second:
the emulator connects it to a pty (fm11 --rsdev 0 SLAVE), this test is the terminal.   usage: fm11/test_fm11_rs.py [path-to-fm11-binary]
(make BOARD=fm11 first)"""
import os, pty, re, select, shutil, subprocess, sys, tempfile, time

HERE = os.path.dirname(os.path.abspath(__file__))
MK = os.path.dirname(HERE)
FM11 = sys.argv[1] if len(sys.argv) > 1 and sys.argv[1] else '/export/nas/archives/FM-11/emulator/Fm11gtk/fm11'
IMG = os.path.join(MK, 'build-fm11-l2', 'mk09.img')
sys.path.insert(0, os.path.join(MK, 'tests'))
import fatlib
fails = total = 0


def check(cond, what, detail=''):
    global fails, total
    total += 1
    fails += not cond
    print(('ok   : ' if cond else 'FAIL : ') + what)
    if not cond and detail:
        print('     ' + detail.replace('\n', '\n     ')[-1200:])


if not os.path.exists(IMG) or not os.path.exists(FM11):
    print('SKIP: build-fm11-l2/mk09.img or the emulator is not there')
    sys.exit(0)
work = tempfile.mkdtemp(prefix='fm11rs_')
img = os.path.join(work, 'd.img')
shutil.copy(IMG, img)
SP = ' ' * 90


def emu(frames, keys, rsdev=None):
    cmd = [FM11, '--headless', str(frames), os.path.join(work, 's.ppm'), '-h0', img, '--dipsw', '0x7F', '--keyat', '3000', '--keygap', '14', '--keys', keys,
           '--text', os.path.join(work, 't.txt'), '--mute']
    if rsdev:
        cmd += ['--rsdev', '0', rsdev]
    return subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# phase A: the first terminal writes /etc/ttys (the digits in it are the lines that get a login), then the disk is looked at from outside
p = emu(9000, 'root|root|echo 1 > /etc/ttys|sync|')
p.wait()
v = fatlib.Volume(img, True)
check((v.read('/etc/ttys') or b'').strip() == b'1', 'the first terminal writes /etc/ttys with the line 1')

# phase B: the second terminal is a pty
master, slave = pty.openpty()
sname = os.ttyname(slave)
p = emu(40000, 'root|root|' + SP + 'id|', sname)
buf = b''


def talk(send, until, timeout=40):
    global buf
    if send:
        os.write(master, send.encode())
    t0 = time.time()
    while until.encode() not in buf and time.time() - t0 < timeout:
        r, _, _ = select.select([master], [], [], 0.5)
        if r:
            try:
                buf += os.read(master, 4096)
            except OSError:
                break
    ok = until.encode() in buf
    out = buf.decode('latin-1').replace('\r', '')
    buf = buf[buf.index(until.encode()) + len(until):] if ok else b''
    return ok, out


time.sleep(1)
ok, r = talk('\r', 'login: ')
check(ok, 'the RS-232C port (the second terminal) gets a login prompt', r)
ok, r = talk('user\r', 'Password: ')
ok, r = talk('user\r', '/home/user> ')
check(ok, 'user logs in on the second terminal, the shell starts in the home directory', r[-300:])
ok, r = talk('id\r', '/home/user> ')
check('uid=100(user) gid=100(users)' in r, 'id on the second terminal: user', r)
ok, r = talk('cd /etc\r', '/etc> ')
ok, r = talk('ls\r', '/etc> ')
check('passwd' in r and 'shadow' in r, 'cd on the second terminal: the program starts in the directory of its shell', r)
ok, r = talk('ls /home\r', '/etc> ')
check('user' in r, 'ls on the second terminal')
ok, r = talk('exit\r', 'login: ')
check(ok, 'exit on the second terminal: the login comes back on that terminal', r)
p.wait()
os.close(master)
os.close(slave)
txt = open(os.path.join(work, 't.txt'), encoding='latin-1', errors='replace').read() if os.path.exists(os.path.join(work, 't.txt')) else ''
check('uid=0(root) gid=0(root)' in txt and 'uid=100' not in txt, 'the first terminal (the screen) is root meanwhile and shows nothing of the second one', txt[-600:])
shutil.rmtree(work, ignore_errors=True)
print('fm11 rs test: %s' % ('PASS' if not fails else 'FAIL'))
sys.exit(1 if fails else 0)
