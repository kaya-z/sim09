#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""libsort.py - put the blocks of a library file (;@def name ... ;@use name ...) in an order in which a block comes before the blocks that it uses.

    libsort.py in.s out.s out.idx

cg reads a library file again for every round of "a block that was taken needs another block"; with this order one round nearly always does it (on MK09 the
rounds were most of the time of a compile)."""
import sys

lines = open(sys.argv[1], encoding='latin-1').read().split('\n')
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
out = head + [l for i in order for l in blocks[i]['text']]
open(sys.argv[2], 'w', encoding='latin-1').write('\n'.join(out) + '\n')
for l in head:
    if l.strip() and not l.startswith('*') and not l.startswith(';'):
        sys.exit('libsort: the text in front of the first block would be lost: ' + l)
# the index: offset-high offset-low length defs;uses
pos = sum(len(l) + 1 for l in head)
idx = []
for i in order:
    b = blocks[i]
    n = sum(len(l) + 1 for l in b['text'])
    uses = [u for u in dict.fromkeys(b['uses']) if u not in b['defs']]
    idx.append('%d %d %d %s;%s' % (pos >> 16, pos & 65535, n, ','.join(b['defs']), ','.join(uses)))
    pos += n
open(sys.argv[3], 'w', encoding='latin-1').write('\n'.join(idx) + '\n')
