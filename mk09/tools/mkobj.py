#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""mkobj.py - make the object file of a library of assembler text (;@def name ... ;@use name ...), and its index.

    mkobj.py as_obj in.s out.obj out.idx

as_obj is `as` built with -DAS_HOST -DAS_OBJ: with -c it assembles one block and prints what a linker needs (#F: a word that has an external name, #R: a word that is an address
of the block, #X: the names that the block defines, #C #B: the sizes of the code and the bss).  The blocks are put in an order in which a block comes before the blocks that
it uses (so cg finds what it needs in one round).

The object file has the code of the blocks, one after the other, as a stream of items (the size of the code of a block is in the index, so there is no header):
    1 n bytes             n bytes of code (1 to 255)
    2 kind hi lo          a word that is an address of the block: kind 0, the start of the code of the block is added; 1, the start of the bss of the block is added
    3 kind n name hi lo   a word that has a name that the block does not define: kind 0, the address of the name is added;
                          kind 1 (the distance of a branch), the address of the name minus the start of the block is added
The index has one line for each block:  offset-high offset-low length code-size bss-size defs;uses;exports  (the exports are name:kind:offset separated by commas;
kind 0 code, 1 bss).  cg gives these to as, which links the blocks.
"""
import os, subprocess, sys, tempfile

as_obj, src, out_obj, out_idx = sys.argv[1:5]
as_obj = os.path.abspath(as_obj)
lines = open(src, encoding='latin-1').read().split('\n')
if lines and lines[-1] == '':
    lines.pop()
head, blocks, cur, header = [], [], None, False
for l in lines:
    if l.startswith(';@def '):
        if cur is None or not header:
            cur = {'defs': [], 'uses': [], 'text': []}
            blocks.append(cur)
        header = True
        cur['defs'].append(l[6:].strip())
        cur['text'].append(l)
        continue
    header = False
    if cur is None:
        head.append(l)
        continue
    cur['text'].append(l)
    if l.startswith(';@use ') or l.startswith(';@ext '):
        cur['uses'].append(l[6:].strip())
for l in head:
    if l.strip() and not l.startswith('*') and not l.startswith(';'):
        sys.exit('mkobj: the text in front of the first block would be lost: ' + l)
where = {}
for i, b in enumerate(blocks):
    for d in b['defs']:
        where[d] = i
seen, order = set(), []


def visit(i):
    if i in seen:
        return
    seen.add(i)
    for u in blocks[i]['uses']:
        if u in where:
            visit(where[u])
    order.append(i)


for i in range(len(blocks)):
    visit(i)
order.reverse()                  # (the users first)


def u16(v):
    return bytes([(v >> 8) & 255, v & 255])


def record(b, tmp):
    sf = os.path.join(tmp, 'b.s')
    o = os.path.join(tmp, 'b.bin')
    open(sf, 'w', encoding='latin-1').write('\n'.join(b['text']) + '\n')
    p = subprocess.run([as_obj, '-c', '-o', o, sf], stdout=subprocess.PIPE, stderr=subprocess.STDOUT, cwd=tmp)
    text = p.stdout.decode('latin-1')
    if p.returncode:
        sys.exit('mkobj: block %s:\n%s' % (b['defs'], text))
    text = text.split('#START\n')[-1]
    exp, ev, csize, bsize = [], {}, 0, 0
    for l in text.split('\n'):
        f = l.split()
        if not f or not f[0].startswith('#'):
            continue
        if f[0] == '#X':
            exp.append((f[1], int(f[2]), int(f[3])))
        elif f[0] == '#F':
            ev[int(f[1])] = (3, int(f[2]), f[3])
        elif f[0] == '#R':
            ev[int(f[1])] = (2, int(f[2]), None)
        elif f[0] == '#C':
            csize = int(f[1])
        elif f[0] == '#B':
            bsize = int(f[1])
    code = open(o, 'rb').read()
    if len(code) != csize:
        sys.exit('mkobj: block %s: %d bytes of code, %d expected' % (b['defs'], len(code), csize))
    out, lit, i = bytearray(), bytearray(), 0

    def flush():
        while lit:
            n = min(len(lit), 255)
            out.extend([1, n]); out.extend(lit[:n]); del lit[:n]

    while i < csize:
        if i in ev:
            kind, k, name = ev[i]
            if i + 2 > csize or i + 1 in ev:
                sys.exit('mkobj: block %s: a word that is not whole at %d' % (b['defs'], i))
            flush()
            if kind == 2:
                out.extend([2, k, code[i], code[i + 1]])
            else:
                nb = name.encode('latin-1')
                out.extend([3, k, len(nb)]); out.extend(nb); out.extend([code[i], code[i + 1]])
            i += 2
        else:
            lit.append(code[i]); i += 1
    flush()
    return bytes(out), csize, bsize, exp


obj = bytearray()
idx = []
with tempfile.TemporaryDirectory() as tmp:
    for i in order:
        b = blocks[i]
        rec, csize, bsize, exp = record(b, tmp)
        uses = [u for u in dict.fromkeys(b['uses']) if u not in b['defs']]
        idx.append('%d %d %d %d %d %s;%s;%s' % (len(obj) >> 16, len(obj) & 65535, len(rec), csize, bsize, ','.join(b['defs']), ','.join(uses),
                                                ','.join('%s:%d:%d' % e for e in exp)))
        obj += rec
open(out_obj, 'wb').write(bytes(obj))
open(out_idx, 'w', encoding='latin-1').write('\n'.join(idx) + '\n')
