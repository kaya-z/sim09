#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkusers.py - the users of the disk image of MK09 2.0:  mkusers.py [--no-expire] TREE OWNERS

Writes TREE/etc/passwd, group and shadow, the home directory and /tmp, and OWNERS (the input of mkdisk.py --owners: who owns what).
The hash of a password is the one of lib/mk_sha256.c: h = SHA-256(salt + password), then COUNT times  h = SHA-256(h + salt + password)
(COUNT = PW_COUNT of include/pwd.h).  The passwords of the first image are the user names (root: root, user: user).  They are marked "must change" (the 4th field of /etc/shadow is 1): `login` asks for a new password the first time; --no-expire
leaves the mark out (the images of the tests).
"""
import hashlib, os, re, sys

HERE = os.path.dirname(os.path.abspath(__file__))
COUNT = int(re.search(r'#define\s+PW_COUNT\s+(\d+)', open(os.path.join(HERE, '..', 'include', 'pwd.h')).read()).group(1))

USERS = [('root', 0, 0, '/', '/bin/sh.mx', 'root', '5a7c3e91'),
         ('user', 100, 100, '/home/user', '/bin/sh.mx', 'user', 'c04b8d26')]
GROUPS = [('root', 0), ('users', 100)]


def pwhash(pw, salt, count=COUNT):
    m = (salt + pw).encode()
    h = hashlib.sha256(m).digest()
    for _ in range(count):
        h = hashlib.sha256(h + m).digest()
    return h.hex()


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    expire = '--no-expire' not in sys.argv
    tree, owners = args[0], args[1]
    etc = os.path.join(tree, 'etc')
    os.makedirs(etc, exist_ok=True)
    os.makedirs(os.path.join(tree, 'tmp'), exist_ok=True)
    os.makedirs(os.path.join(tree, 'home', 'user'), exist_ok=True)
    with open(os.path.join(etc, 'passwd'), 'w') as f:
        for n, u, g, home, sh, _, _ in USERS:
            f.write('%s:%d:%d:%s:%s\n' % (n, u, g, home, sh))
    with open(os.path.join(etc, 'ttys'), 'w') as f:                        # the lines (terminals) that get a login besides the first one: their numbers
        f.write('# the terminals besides the first one: write the numbers of their lines here, one digit each\n')
    with open(os.path.join(etc, 'group'), 'w') as f:
        for n, g in GROUPS:
            f.write('%s:%d\n' % (n, g))
    with open(os.path.join(etc, 'shadow'), 'w') as f:
        for n, u, g, home, sh, pw, salt in USERS:
            f.write('%s:%s:%s%s\n' % (n, salt, pwhash(pw, salt), ':1' if expire else ''))
    with open(owners, 'w') as f:
        f.write('# pattern uid gid mode\n')
        f.write('etc/shadow 0 0 600\n')
        f.write('bin/passwd.mx 0 0 1755\nbin/su.mx 0 0 1755\n')
        f.write('bin/format.mx 0 0 2700\n')                  # (the only program that sends to the disk server)
        for n in ('reboot', 'mount', 'umount', 'hd', 'runas'):
            f.write('bin/%s.mx 0 0 700\n' % n)
        f.write('tmp 0 0 777\n')
        f.write('home/user 100 100 755\nhome/user/* 100 100 644\n')


if __name__ == '__main__':
    main()
