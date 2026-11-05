#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkl2disk.py [-s SECTORS] [-o OUT] [BASE.dsk]  - the disk of the Level 2 machine of the FPGA board (the same bitstream as FUZIX).

The 256 byte boot ROM of the bitstream loads the code from LSN 2 (as it does for FUZIX and MK09): the loader of
os9/level2/l2boot.asm (one record, LSN 2, and the entry record, LSN 3).  The loader switches the machine to the Level 2
mode, reads the ROM image of the Level 2 machine (os9/os9v2_tn9k.rom: 112 sectors, LSN 1100-1211, marked used in the
allocation map) into the physical memory and starts it.  The disk is /d0 of the Level 2 system.

The file system is made here (no simulator needed): the files of os9/OS9.dsk (the Level 1 disk) are read, the commands that have a Level 2
version (os9/level2/cmds: the ones that look at the memory and the system: dir, mdir, mfree, procs, ident, jwp ...), the Pascal and C compilers
(os9/pascal: the library in /d0/LIB, the samples in /d0/PAS and /d0/C) and the headers of the C compiler (/d0/INC) are put over them, and everything
is written to a new disk of 4096 sectors (default) with clusters of two sectors (DD.BIT = 2: the allocation map stays one sector, as the loader at LSN 2
needs).  Every file and directory starts on a cluster boundary and uses whole clusters, as RBF does when it allocates, so that the files can be
deleted on the disk.  The other commands (build, rename, display, tmode ...) are the same on both levels."""
import os, shutil, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SEC = 256
IMGLSN, NSECT = 1100, 112        # the ROM image of the Level 2 machine
ATT_DIR, ATT_CMD, ATT_FILE = 0xBF, 0x2D, 0x0B      # d-ewrewr, e pe r pr, w r pr


class Node:
    def __init__(self, name, attr, data=None, children=None, owner=0, mtime=None, ctime=None):
        self.name, self.attr, self.data, self.owner = name, attr, data, owner
        self.children = children          # a list of Nodes for a directory, else None
        now = time.localtime()
        self.mtime = mtime or bytes([now.tm_year - 1900, now.tm_mon, now.tm_mday, now.tm_hour, now.tm_min])
        self.ctime = ctime or self.mtime[:3]

    def child(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None


def read_tree(d):
    def sec(n): return d[n * SEC:(n + 1) * SEC]

    def node(fd, name):
        f = sec(fd)
        size = int.from_bytes(f[9:13], 'big')
        data = b''
        for i in range(48):
            o = 16 + 5 * i
            l, c = int.from_bytes(f[o:o + 3], 'big'), int.from_bytes(f[o + 3:o + 5], 'big')
            if c == 0:
                break
            data += d[l * SEC:(l + c) * SEC]
        data = data[:size]
        n = Node(name, f[0], mtime=f[3:8], ctime=f[13:16], owner=int.from_bytes(f[1:3], 'big'))
        if f[0] & 0x80:
            n.children = []
            for i in range(0, len(data), 32):
                e = data[i:i + 32]
                if e[0] == 0:
                    continue
                end = next(k for k, b in enumerate(e[:29]) if b & 0x80)          # the last character has bit 7 set
                nm = ''.join(chr(b & 0x7f) for b in e[:end + 1])
                if nm in ('.', '..'):
                    continue
                n.children.append(node(int.from_bytes(e[29:32], 'big'), nm))
        else:
            n.data = data
        return n
    return node(int.from_bytes(d[8:11], 'big'), '')


class Disk:
    def __init__(self, size, clu):
        self.size, self.clu = size, clu
        self.img = bytearray(size * SEC)
        self.used = [False] * (size // clu)

    def reserve(self, lsn, n):
        for c in range(lsn // self.clu, (lsn + n + self.clu - 1) // self.clu):
            self.used[c] = True

    def alloc(self, nsec):
        """the first run of free clusters for nsec sectors: the LSN"""
        need = (nsec + self.clu - 1) // self.clu
        c = 0
        while c + need <= len(self.used):
            taken = [k for k in range(c, c + need) if self.used[k]]
            if not taken:
                for k in range(c, c + need):
                    self.used[k] = True
                return c * self.clu, need * self.clu
            c = taken[-1] + 1
        sys.exit('mkl2disk: the disk is full (%d sectors)' % self.size)

    def write(self, n, parent_fd):
        """write a node: its FD, its data (a directory: the room first, as RBF does, then the entries); returns the LSN of the FD"""
        fdl, _ = self.alloc(1)
        isdir = n.children is not None
        nbytes = 32 * (2 + len(n.children)) if isdir else len(n.data)
        seg = b''
        if nbytes:
            l, cnt = self.alloc((nbytes + SEC - 1) // SEC)
            seg = l.to_bytes(3, 'big') + cnt.to_bytes(2, 'big')
        if isdir:
            data = bytearray()
            for nm, lsn in (('..', parent_fd if parent_fd is not None else fdl), ('.', fdl)):
                data += self.entry(nm, lsn)
            for c in n.children:
                data += self.entry(c.name, self.write(c, fdl))
            data = bytes(data)
        else:
            data = n.data
        if data:
            self.img[l * SEC:l * SEC + len(data)] = data
        fd = bytearray(SEC)
        fd[0] = n.attr
        fd[1:3] = n.owner.to_bytes(2, 'big')
        fd[3:8] = n.mtime
        fd[8] = 2 if (isdir and parent_fd is None) else 1
        fd[9:13] = len(data).to_bytes(4, 'big')
        fd[13:16] = n.ctime
        fd[16:16 + len(seg)] = seg
        self.img[fdl * SEC:(fdl + 1) * SEC] = fd
        return fdl

    @staticmethod
    def entry(name, lsn):
        b = bytearray(name.encode('latin-1'))
        b[-1] |= 0x80
        return bytes(b) + bytes(29 - len(b)) + lsn.to_bytes(3, 'big')


def main():
    args = sys.argv[1:]
    size, out = 4096, os.path.join(ROOT, 'os9', 'OS9L2.dsk')
    while args and args[0].startswith('-'):
        if args[0] == '-s': size = int(args[1])
        elif args[0] == '-o': out = args[1]
        else: sys.exit(__doc__)
        args = args[2:]
    base = args[0] if args else os.path.join(ROOT, 'os9', 'OS9.dsk')
    src = bytes(open(base, 'rb').read())
    clu = 1
    while size // clu > 2048:           # (one sector of allocation map: 2048 clusters at most)
        clu *= 2
    root = read_tree(src)

    # ---- the commands that have a Level 2 version, the Pascal and C compilers, the libraries, the headers, the samples
    cdir = os.path.join(ROOT, 'os9', 'level2', 'cmds')
    built = set()
    for f in sorted(os.listdir(cdir)):
        p = os.path.join(cdir, f)
        if os.path.isfile(p) and open(p, 'rb').read(2) == b'\x87\xcd':
            built.add(f)
    built.add('shutdown')
    have = "as attr basic09 cg cmp copy date dhry del deldir dir dump echo free ident jwp cc cc1 cpp link list load login makdir mdir mfree pas0 pas1 pc procs pwd pxd reboot runb shutdown sleep tee tsmon unlink ysdbasic".split()
    names = [n for n in have if n in built]
    cmds = root.child('CMDS')
    for n in names:
        old = cmds.child(n)
        if old:
            cmds.children.remove(old)
        cmds.children.append(Node(n, ATT_CMD, data=open(os.path.join(cdir, n), 'rb').read()))

    def put_dir(name, srcdir, files):
        old = root.child(name)
        if old:
            root.children.remove(old)
        root.children.append(Node(name, ATT_DIR, children=[Node(f, ATT_FILE, data=open(os.path.join(srcdir, f), 'rb').read()) for f in files]))
    pas = os.path.join(ROOT, 'os9', 'pascal')
    for name, sub in (('LIB', 'lib'), ('PAS', 'samples'), ('C', 'csamples')):
        p = os.path.join(pas, sub)
        if os.path.isdir(p):
            put_dir(name, p, sorted(os.listdir(p)))
    inc = os.path.join(ROOT, 'mk09', 'disk', 'inc')
    if os.path.isdir(inc):
        put_dir('INC', inc, sorted(f for f in os.listdir(inc) if f.endswith('.h') and f != 'mklib.h'))

    # ---- the new disk
    d = Disk(size, clu)
    d.reserve(0, 2 + 2)                         # LSN 0 (the label), LSN 1 (the allocation map), LSN 2 and 3 (the loader)
    d.reserve(IMGLSN, NSECT)                    # the ROM image
    root_fd = d.write(root, None)
    # the loader (a09 -o: no prefix, the code starts at $0300) and the ROM image
    import subprocess, tempfile
    ldr = os.path.join(tempfile.gettempdir(), 'l2boot.bin')
    subprocess.run([os.path.join(ROOT, 'tools', 'a09'), '-o', ldr, os.path.join(ROOT, 'os9', 'level2', 'l2boot.asm')], capture_output=True)
    code = open(ldr, 'rb').read()
    rom = open(os.path.join(ROOT, 'os9', 'os9v2_tn9k.rom'), 'rb').read()
    if len(rom) > NSECT * SEC or len(code) > 253 or IMGLSN + NSECT > size:
        sys.exit('mkl2disk: the ROM image (%d bytes) or the loader (%d bytes) does not fit: see os9/level2/l2boot.asm' % (len(rom), len(code)))
    img = d.img
    img[2 * SEC:3 * SEC] = bytes([0x03, 0x00, len(code)]) + code + bytes(253 - len(code))        # [dest $0300][n][code]
    img[3 * SEC:4 * SEC] = bytes([0x03, 0x00, 0x00]) + bytes(253)                                  # [entry $0300][0]
    img[IMGLSN * SEC:IMGLSN * SEC + len(rom)] = rom
    # LSN 0: the label of the base disk (name, format, tracks ...) with the new size and the map
    lab = bytearray(src[:SEC])
    lab[0:3] = size.to_bytes(3, 'big')
    lab[4:6] = (size // clu // 8).to_bytes(2, 'big')
    lab[6:8] = clu.to_bytes(2, 'big')
    lab[8:11] = root_fd.to_bytes(3, 'big')
    lab[21:26] = bytes(5)                          # no boot file (DD.BT, DD.BSZ): the machine starts from the ROM image
    img[0:SEC] = lab
    bm = bytearray(SEC)
    for c, u in enumerate(d.used):
        if u:
            bm[c // 8] |= 0x80 >> (c % 8)
    img[SEC:2 * SEC] = bm
    open(out, 'wb').write(img)
    free = sum(1 for u in d.used if not u) * clu
    big = run = 0
    for u in d.used + [True]:
        run = 0 if u else run + 1
        big = max(big, run)
    print('%s: %d sectors (%d-sector clusters), %d commands replaced; %d free sectors, largest block %d sectors' % (out, size, clu, len(names), free, big * clu))


main()
