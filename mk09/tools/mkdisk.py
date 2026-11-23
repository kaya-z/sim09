#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkdisk.py - make an MK09 disk image (the content of the SD card).

    mkdisk.py -o disk.img [--boot boot.img] [--size-mb 16] [--spc N] [--label MK09] TREE

Layout (512 byte blocks):
    block 0          MBR with one partition (type 06, FAT16) that starts at block 2048 (1MB)
    block 1 ...      the boot image (sectors of 256 bytes from sector 2 on, see mkimage.py)
    block 2048 ...   the FAT16 volume with the files of the directory tree TREE

The volume has up to 2GB (FAT16: at most 65524 clusters of up to 64 blocks; --spc is chosen so that the
clusters are not too many: 4 blocks as long as that fits).  The image is built in memory.  The
files are written one after the other, so every file occupies consecutive clusters (the FS
server finds the position in such a file without walking the FAT).  Names are converted to
8.3 (upper case); host names that do not fit are an error."""
import argparse, os, struct, sys, time

BLK = 512
PART_START = 2048          # (--part-start)
OWNERS = []                # (--owners: [(pattern, uid, gid, mode)] of the entries that are not root's rwxr-xr-x)
KEEP_CASE = False          # (--keep-case: 2.0, the names are written as they are, they are case sensitive)
SMALL = False              # (--small: fewer than 4085 clusters allowed, for the 2HD floppy of the FM-11)


def name83(name):
    base, dot, ext = name.partition('.')
    if '.' in ext or len(base) > 8 or len(ext) > 3 or not base:
        sys.exit('mkdisk: name does not fit 8.3: %r' % name)
    for ch in name:
        if ch in '"*+,/:;<=>?[\\]| ' or ord(ch) < 33:
            sys.exit('mkdisk: bad character in name %r' % name)
    if KEEP_CASE:
        return (base.ljust(8) + ext.ljust(3)).encode('latin-1')
    return (base.upper().ljust(8) + ext.upper().ljust(3)).encode('latin-1')


def fat_time(t):
    # MK09_FIXED_TIME=<seconds>: every time stamp is this one (a reproducible image; tools/v1guard.sh)
    if os.environ.get('MK09_FIXED_TIME'):
        t = float(os.environ['MK09_FIXED_TIME'])
    tm = time.localtime(t)
    d = ((tm.tm_year - 1980) << 9) | (tm.tm_mon << 5) | tm.tm_mday
    s = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec // 2)
    return d, s


class Fat16:
    def __init__(self, blocks, spc, label):
        self.blocks, self.spc = blocks, spc
        self.rootents = 512
        self.rootsecs = self.rootents * 32 // BLK
        self.rsvd = 1
        self.nfats = 2
        # fat size: iterate
        fatsz = 1
        while True:
            data = blocks - self.rsvd - self.nfats * fatsz - self.rootsecs
            ncl = data // spc
            need = ((ncl + 2) * 2 + BLK - 1) // BLK
            if need <= fatsz:
                break
            fatsz = need
        self.fatsz = fatsz
        self.ncl = ncl
        if not (SMALL and 16 <= ncl < 65525) and not (4085 <= ncl < 65525):
            sys.exit('mkdisk: %d clusters is not FAT16 (change --spc or --size-mb)' % ncl)
        self.fat_blk = self.rsvd
        self.root_blk = self.fat_blk + self.nfats * fatsz
        self.data_blk = self.root_blk + self.rootsecs
        self.img = bytearray(blocks * BLK)
        self.fat = [0] * (ncl + 2)
        self.fat[0], self.fat[1] = 0xFFF8, 0xFFFF
        self.next = 2
        self.root = bytearray(self.rootsecs * BLK)
        self.label = label
        self.now = fat_time(time.time())

    def alloc(self, n):
        if self.next + n > self.ncl + 2:
            sys.exit('mkdisk: the volume is full')
        first = self.next
        for i in range(n):
            self.fat[first + i] = first + i + 1 if i + 1 < n else 0xFFFF
        self.next += n
        return first

    def write_clusters(self, first, data):
        off = (self.data_blk + (first - 2) * self.spc) * BLK
        self.img[off:off + len(data)] = data

    def entry(self, name, attr, first, size, mtime):
        d, t = fat_time(mtime) if mtime else self.now
        return struct.pack('<11sBBBHHHHHHHI', name, attr, 0, 0, t, d, d, 0, t, d, first, size)

    def add_dir(self, entries, ent):
        entries.append(ent)

    def owned(self, ent, rel):
        """2.0: owner (byte 20), group + 1 (byte 21) and permissions (bytes 14-15) of an entry, by the first pattern of --owners that fits"""
        import fnmatch
        for pat, uid, gid, mode in OWNERS:
            if fnmatch.fnmatch(rel, pat):
                b = bytearray(ent)
                b[14:16] = struct.pack('<H', mode)
                b[20] = uid
                b[21] = gid + 1
                return bytes(b)
        return ent

    def build_dir(self, path, parent_cl, self_cl, rel=''):
        """returns the directory content (bytes) with entries for the files and subdirs of path"""
        ents = []
        if self_cl:
            ents.append(self.entry(b'.          ', 0x10, self_cl, 0, 0))
            ents.append(self.entry(b'..         ', 0x10, parent_cl, 0, 0))
        for nm in sorted(n for n in os.listdir(path) if not n.startswith(".nfs")):   # (an NFS file that is removed while a process has it open)
            full = os.path.join(path, nm)
            n83 = name83(nm)
            if os.path.isdir(full):
                # clusters of the directory first, then its content
                sub_bytes_len = None
                cl = self.alloc(1)           # placeholder: a directory grows beyond one cluster rarely;
                content = self.build_dir(full, self_cl, cl, rel + nm + '/')
                need = (len(content) + self.spc * BLK - 1) // (self.spc * BLK)
                if need > 1:
                    # extend the chain contiguously (nothing was allocated after 'cl' except subdirs...)
                    sys.exit('mkdisk: directory %s too big' % full)
                self.write_clusters(cl, content.ljust(self.spc * BLK, b'\0'))
                ents.append(self.owned(self.entry(n83, 0x10, cl, 0, os.stat(full).st_mtime), rel + nm))
            else:
                data = open(full, 'rb').read()
                first = 0
                if data:
                    n = (len(data) + self.spc * BLK - 1) // (self.spc * BLK)
                    first = self.alloc(n)
                    self.write_clusters(first, data)
                ents.append(self.owned(self.entry(n83, 0x20, first, len(data), os.stat(full).st_mtime), rel + nm))
        return b''.join(ents)

    def finish(self, tree):
        label = self.entry((self.label.upper().ljust(11)).encode('latin-1')[:11], 0x08, 0, 0, 0)
        content = label + self.build_dir(tree, 0, 0)
        if len(content) > len(self.root):
            sys.exit('mkdisk: too many files in the root directory')
        self.root[:len(content)] = content
        # BPB
        b = bytearray(BLK)
        b[0:3] = b'\xEB\x3C\x90'
        b[3:11] = b'MK09    '
        struct.pack_into('<HBHBHHBHHHII', b, 11, BLK, self.spc, self.rsvd, self.nfats, self.rootents,
                         self.blocks if self.blocks < 65536 else 0, 0xF8, self.fatsz, 32, 64, PART_START,
                         self.blocks if self.blocks >= 65536 else 0)
        b[36] = 0x80
        b[38] = 0x29
        b[39:43] = struct.pack('<I', 0x4D4B0900)
        b[43:54] = self.label.upper().ljust(11).encode('latin-1')[:11]
        b[54:62] = b'FAT16   '
        b[510:512] = b'\x55\xAA'
        self.img[0:BLK] = b
        fat = bytearray()
        for v in self.fat:
            fat += struct.pack('<H', v)
        fat = fat.ljust(self.fatsz * BLK, b'\0')
        for k in range(self.nfats):
            o = (self.fat_blk + k * self.fatsz) * BLK
            self.img[o:o + len(fat)] = fat
        o = self.root_blk * BLK
        self.img[o:o + len(self.root)] = self.root
        return bytes(self.img)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', '--output', required=True)
    ap.add_argument('--boot')
    ap.add_argument('--ipl', help='code for the start of the MBR (FM-11: the boot ROM loads the first 512 bytes of the disk to $0400 and runs them; at most 446 bytes)')
    ap.add_argument('--size-mb', type=float, default=16)
    ap.add_argument('--spc', type=int, default=0, help='blocks per cluster (a power of 2 up to 64); 0: the smallest of 4, 8, ... 64 that makes the clusters fewer than 65525')
    ap.add_argument('--label', default='MK09')
    ap.add_argument('--part-start', type=int, default=2048, help='first block of the volume (the boot image is in front of it)')
    ap.add_argument('--total-blocks', type=int, default=0, help='the size of the whole disk in blocks (instead of --size-mb)')
    ap.add_argument('--small', action='store_true', help='allow a volume with fewer than 4085 clusters (FAT16 layout, MK09 only)')
    ap.add_argument('--keep-case', action='store_true', help='write the names as they are (MK09 2.0: case sensitive names); without it they are made upper case')
    ap.add_argument('--l2card', nargs=2, metavar=('ROMIMG', 'LOADER'), help='the card of the FPGA board for the machine with the MMU: the loader (boot/l2loader.s, raw) at LSN 2, the ROM image (8KB, boot/bootrom_l2.s) at LSN 8, the system (--boot) at LSN 64')
    ap.add_argument('--owners', help='2.0: a file with lines "pattern uid gid mode(octal)": the owner, group and permissions of the entries (the patterns are relative paths, as fnmatch; the first that fits)')
    ap.add_argument('tree')
    a = ap.parse_args()
    global PART_START, SMALL, KEEP_CASE
    KEEP_CASE = a.keep_case
    if a.owners:
        for ln in open(a.owners):
            f = ln.split()
            if len(f) == 4 and not f[0].startswith('#'):
                OWNERS.append((f[0], int(f[1]), int(f[2]), int(f[3], 8)))
    PART_START = a.part_start
    SMALL = a.small
    vblocks = min(int(a.size_mb * 1024 * 1024) // BLK, 65524 * 64 + 600)
    if a.total_blocks:
        vblocks = a.total_blocks - PART_START
    spc = a.spc or 4
    while not a.spc and spc < 64 and vblocks // spc >= 65500:
        spc *= 2
    fs = Fat16(vblocks, spc, a.label)
    vol = fs.finish(a.tree)
    disk = bytearray((PART_START + vblocks) * BLK)
    # MBR: partition 1
    mbr = bytearray(BLK)
    pe = struct.pack('<B3sB3sII', 0x80, b'\x00\x01\x01', 0x06, b'\xFE\xFF\xFF', PART_START, vblocks)
    if a.ipl:
        ipl = open(a.ipl, 'rb').read()
        if len(ipl) > 0x1BE:
            sys.exit('mkdisk: the IPL code does not fit before the partition table')
        mbr[0:len(ipl)] = ipl
    mbr[0x1BE:0x1BE + 16] = pe
    mbr[510:512] = b'\x55\xAA'
    disk[0:BLK] = mbr
    if a.boot:
        boot = open(a.boot, 'rb').read()
        if not a.l2card and len(boot) > (PART_START - 1) * BLK:
            sys.exit('mkdisk: the boot image is too big')
        if a.l2card:
            rom = open(a.l2card[0], 'rb').read()
            ldr = open(a.l2card[1], 'rb').read()
            if len(rom) > 32 * 256 or len(ldr) > 253:
                sys.exit('mkdisk: the ROM image (8KB) or the loader (253 bytes) is too big')
            disk[2 * 256:2 * 256 + 3 + len(ldr)] = bytes([0x03, 0x00, len(ldr)]) + ldr      # the record: to $0300
            disk[3 * 256:3 * 256 + 3] = bytes([0x03, 0x00, 0x00])                           # the entry record: $0300
            disk[8 * 256:8 * 256 + len(rom)] = rom
            if 64 * 256 + len(boot) > PART_START * BLK:
                sys.exit('mkdisk: the boot image is too big')
            disk[64 * 256:64 * 256 + len(boot)] = boot
        else:
            disk[2 * 256:2 * 256 + len(boot)] = boot
    disk[PART_START * BLK:] = vol
    open(a.output, 'wb').write(disk)
    used = fs.next - 2
    print('disk image %s: partition %d blocks, %d clusters of %d blocks, %d used' % (a.output, vblocks, fs.ncl, fs.spc, used))


main()
