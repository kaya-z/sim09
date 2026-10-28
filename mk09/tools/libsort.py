#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""libsort.py - put the blocks of a library file (;@def name ... ;@use name ...) in an order in which a block comes before the blocks that it uses.

    libsort.py in.s out.s

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
