#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""msgsplit.py - take the error messages out of a C program (so that it fits in the transient area of MK09).

    msgsplit.py in.c out.c out.msg

Every  err("text")  and  expect(token, "text")  of in.c gets a number N (1 up) instead of the text; out.msg has the texts, one per line (line N is
message N); out.c has  #define MSGS 1  in front, which makes the program print message N by reading that line of /LIB/<name>.MSG."""
import re, sys

src = open(sys.argv[1], encoding='utf-8').read()
msgs = []
index = {}


def num(text):
    if text not in index:
        msgs.append(text)
        index[text] = len(msgs)
    return index[text]


def sub_err(m):
    return 'err(%d)' % num(m.group(1))


def sub_expect(m):
    return 'expect(%s, %d)' % (m.group(1), num(m.group(2)))


lit = r'"((?:[^"\\]|\\.)*)"'
out = re.sub(r'\berr\(' + lit + r'\)', sub_err, src)
charlit = r"'(?:[^'\\]|\\.)'"
out = re.sub(r'\bexpect\(((?:' + charlit + r'|[^,()\'"]|\([^()]*\))*), ' + lit + r'\)', sub_expect, out)
if len(msgs) > 255:
    sys.exit('msgsplit: more than 255 messages')
open(sys.argv[2], 'w', encoding='utf-8').write('#define MSGS 1\n' + out)
text = '\n'.join(m.encode().decode('unicode_escape') for m in msgs) + '\n'
open(sys.argv[3], 'w', encoding='utf-8').write(text)
print('msgsplit: %d messages, %d bytes' % (len(msgs), len(text)))
