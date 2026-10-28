#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 YUKARI Semiconductor Devices
"""irsim.py - run a program in the IR of docs/IR.md directly (to test cc1 without the code generator).

    irsim.py file.ir [-m maxsteps]

main is called without arguments; its value is the exit code.  Memory is 64 KB of bytes (16 bit words, big endian).  Library functions
that a program may call without defining them: putchar, puts, getchar, printf (%d %u %x %c %s %%, a width), strlen, strcpy, strcmp,
memcpy, memset, abs, exit."""
import re, sys


class Halt(Exception):
    def __init__(self, code):
        self.code = code


def s16(v):
    v &= 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


class Machine:
    def __init__(self, text, maxsteps=5_000_000, stdin=b''):
        self.mem = bytearray(65536)
        self.syms = {}
        self.funcs = {}
        self.out = []
        self.inp = stdin
        self.inpos = 0
        self.steps = 0
        self.maxsteps = maxsteps
        self.heap = 0x0100
        self.sp = 0xF000        # the frames grow down from here
        self.load(text)

    def alloc(self, n):
        a = self.heap
        self.heap += n
        if self.heap > 0x8000:
            raise RuntimeError('out of data memory')
        return a

    def w16(self, a, v):
        self.mem[a & 0xFFFF] = (v >> 8) & 0xFF
        self.mem[(a + 1) & 0xFFFF] = v & 0xFF

    def r16(self, a):
        return (self.mem[a & 0xFFFF] << 8) | self.mem[(a + 1) & 0xFFFF]

    def cstr(self, a):
        s = bytearray()
        while self.mem[a & 0xFFFF]:
            s.append(self.mem[a & 0xFFFF])
            a += 1
        return bytes(s)

    def load(self, text):
        lines = [re.sub(r'\s*;.*$', '', l).rstrip() for l in text.split('\n')]
        lines = [l for l in lines if l.strip()]
        i = 0
        fixups = []
        while i < len(lines):
            f = lines[i].split()
            if f[0] == '.extern':
                i += 1
            elif f[0] == '.bss':
                self.syms[f[1]] = self.alloc(int(f[2]))
                i += 1
            elif f[0] == '.data':
                a = self.heap
                self.syms[f[1]] = a
                i += 1
                while lines[i].split()[0] != '.end':
                    t = lines[i].strip()
                    op = t.split()[0]
                    arg = t[len(op):].strip()
                    if op == '.b':
                        for v in arg.split(','):
                            self.mem[self.alloc(1)] = int(v) & 0xFF
                    elif op == '.w':
                        for v in arg.split(','):
                            self.w16(self.alloc(2), int(v))
                    elif op == '.wa':
                        m = re.match(r'([A-Za-z_][\w]*)([+-]\d+)?$', arg)
                        fixups.append((self.alloc(2), m.group(1), int(m.group(2) or 0)))
                    elif op == '.s':
                        for b in self.parse_str(arg):
                            self.mem[self.alloc(1)] = b
                    elif op == '.z':
                        self.alloc(int(arg))
                    else:
                        raise RuntimeError('bad data line: ' + t)
                    i += 1
                i += 1
            elif f[0] == '.func':
                name = f[1]
                frame = int(f[2])
                body = []
                i += 1
                while lines[i].split()[0] != '.end':
                    body.append(lines[i].strip())
                    i += 1
                i += 1
                self.funcs[name] = self.prepare(name, frame, body)
            else:
                raise RuntimeError('bad line: ' + lines[i])
        for a, n, off in fixups:
            self.w16(a, self.address(n) + off)

    def address(self, n):
        if n in self.syms:
            return self.syms[n]
        if n in self.funcs or n in BUILTINS:
            if n not in self.syms:
                self.syms[n] = 0x9000 + len([k for k in self.syms if self.syms[k] >= 0x9000]) * 2
                self.faddr = getattr(self, 'faddr', {})
                self.faddr[self.syms[n]] = n
            return self.syms[n]
        raise RuntimeError('undefined: ' + n)

    @staticmethod
    def parse_str(arg):
        assert arg[0] == '"' and arg[-1] == '"', arg
        s = arg[1:-1]
        out = bytearray()
        i = 0
        while i < len(s):
            if s[i] == '\\':
                if s[i + 1] == 'x':
                    out.append(int(s[i + 2:i + 4], 16))
                    i += 4
                else:
                    out.append({'n': 10, 't': 9, '\\': 92, '"': 34}[s[i + 1]])
                    i += 2
            else:
                out.append(ord(s[i]))
                i += 1
        return out

    def prepare(self, name, frame, body):
        labels = {}
        alias = {}
        code = []
        for t in body:
            if t.endswith(':') and ' ' not in t:
                labels[t[:-1]] = len(code)
            else:
                f = t.split()
                if f[0] == 'alias':
                    alias[f[1]] = f[2]
                else:
                    code.append(f)
        for a, b in alias.items():
            while b in alias:
                b = alias[b]
            labels[a] = labels[b]
        return (frame, code, labels)

    def call(self, name, args):
        """args: list of values, the first argument first"""
        if name in BUILTINS:
            return BUILTINS[name](self, args)
        frame, code, labels = self.funcs[name]
        np = len(args)
        base = self.sp - (np * 2 + frame)
        if base < 0x9100:
            raise RuntimeError('stack overflow')
        saved = self.sp
        self.sp = base
        for i, v in enumerate(args):
            self.w16(base + 2 * i, v)
        for k in range(frame):
            self.mem[base + np * 2 + k] = 0
        loc = base + np * 2
        st = []
        pc = 0
        mem = self.mem
        try:
            while pc < len(code):
                self.steps += 1
                if self.steps > self.maxsteps:
                    raise RuntimeError('too many steps')
                f = code[pc]
                pc += 1
                op = f[0]
                if op == 'const':
                    st.append(int(f[1]) & 0xFFFF)
                elif op == 'addr':
                    m = re.match(r'([A-Za-z_]\w*)([+-]\d+)?$', f[1])
                    st.append((self.address(m.group(1)) + int(m.group(2) or 0)) & 0xFFFF)
                elif op == 'laddr':
                    st.append(loc + int(f[1]))
                elif op == 'paddr':
                    st.append(base + 2 * int(f[1]))
                elif op == 'load.w':
                    st.append(self.r16(st.pop()))
                elif op == 'load.b':
                    v = mem[st.pop()]
                    st.append((v - 256 if v >= 128 else v) & 0xFFFF)
                elif op == 'load.ub':
                    st.append(mem[st.pop()])
                elif op == 'store.w':
                    v = st.pop()
                    a = st.pop()
                    self.w16(a, v)
                    st.append(v)
                elif op == 'store.b':
                    v = st.pop()
                    a = st.pop()
                    mem[a] = v & 0xFF
                    st.append(v)
                elif op == 'postinc':
                    a = st.pop()
                    old = self.r16(a)
                    self.w16(a, (old + int(f[1])) & 0xFFFF)
                    st.append(old)
                elif op == 'dup':
                    st.append(st[-1])
                elif op == 'drop':
                    st.pop()
                elif op == 'swap':
                    st[-1], st[-2] = st[-2], st[-1]
                elif op == 'over':
                    st.append(st[-2])
                elif op in BIN:
                    b = st.pop()
                    a = st.pop()
                    st.append(BIN[op](a, b) & 0xFFFF)
                elif op in ('neg', 'com', 'not', 'sx', 'zx'):
                    a = st.pop()
                    if op == 'neg':
                        a = -a
                    elif op == 'com':
                        a = ~a
                    elif op == 'not':
                        a = int(a == 0)
                    elif op == 'sx':
                        a = (a & 0xFF) - 256 if a & 0x80 else a & 0xFF
                    else:
                        a &= 0xFF
                    st.append(a & 0xFFFF)
                elif op == 'jmp':
                    pc = labels[f[1]]
                elif op == 'jz':
                    if st.pop() == 0:
                        pc = labels[f[1]]
                elif op == 'jnz':
                    if st.pop() != 0:
                        pc = labels[f[1]]
                elif op == 'call' or op == 'calli':
                    n = int(f[2] if op == 'call' else f[1])
                    a = [st.pop() for _ in range(n)]
                    if op == 'call':
                        target = f[1]
                    else:
                        target = self.faddr[st.pop()]
                    st.append(self.call(target, a) & 0xFFFF)
                elif op == 'ret':
                    return st.pop()
                elif op == 'copy':
                    n = int(f[1])
                    s = st.pop()
                    d = st.pop()
                    mem[d:d + n] = mem[s:s + n]
                    st.append(d)
                else:
                    raise RuntimeError('bad instruction: ' + ' '.join(f))
            return 0
        finally:
            self.sp = saved


def sdiv(a, b):
    a, b = s16(a), s16(b)
    if b == 0:
        return 0
    q = abs(a) // abs(b)
    return -q if (a < 0) != (b < 0) else q


def smod(a, b):
    a, b = s16(a), s16(b)
    if b == 0:
        return 0
    return a - sdiv(a, b) * b


BIN = {
    'add': lambda a, b: a + b, 'sub': lambda a, b: a - b, 'mul': lambda a, b: a * b,
    'div': sdiv, 'mod': smod,
    'udiv': lambda a, b: a // b if b else 0, 'umod': lambda a, b: a % b if b else 0,
    'and': lambda a, b: a & b, 'or': lambda a, b: a | b, 'xor': lambda a, b: a ^ b,
    'shl': lambda a, b: a << b if b < 16 else 0, 'shr': lambda a, b: s16(a) >> b if b < 16 else (-1 if s16(a) < 0 else 0),
    'ushr': lambda a, b: a >> b if b < 16 else 0,
    'eq': lambda a, b: int(a == b), 'ne': lambda a, b: int(a != b),
    'lt': lambda a, b: int(s16(a) < s16(b)), 'le': lambda a, b: int(s16(a) <= s16(b)),
    'gt': lambda a, b: int(s16(a) > s16(b)), 'ge': lambda a, b: int(s16(a) >= s16(b)),
    'ult': lambda a, b: int(a < b), 'ule': lambda a, b: int(a <= b), 'ugt': lambda a, b: int(a > b), 'uge': lambda a, b: int(a >= b),
}


def b_putchar(m, a):
    m.out.append(a[0] & 0xFF)
    return a[0]


def b_puts(m, a):
    m.out.extend(m.cstr(a[0]) + b'\n')
    return 0


def b_getchar(m, a):
    if m.inpos >= len(m.inp):
        return 0xFFFF
    c = m.inp[m.inpos]
    m.inpos += 1
    return c


def b_printf(m, a):
    fmt = m.cstr(a[0]).decode('latin-1')
    args = list(a[1:])
    out = ''
    i = 0
    while i < len(fmt):
        c = fmt[i]
        if c != '%':
            out += c
            i += 1
            continue
        i += 1
        flags = ''
        while fmt[i] in '-0':
            flags += fmt[i]
            i += 1
        width = ''
        while fmt[i].isdigit():
            width += fmt[i]
            i += 1
        t = fmt[i]
        i += 1
        if t == '%':
            out += '%'
            continue
        v = args.pop(0)
        if t == 'd':
            s = str(s16(v))
        elif t == 'u':
            s = str(v)
        elif t == 'x':
            s = '%x' % v
        elif t == 'c':
            s = chr(v & 0xFF)
        elif t == 's':
            s = m.cstr(v).decode('latin-1')
        else:
            raise RuntimeError('printf: %' + t)
        w = int(width) if width else 0
        if '-' in flags:
            s = s.ljust(w)
        elif '0' in flags and t != 's':
            s = s.rjust(w, '0')
        else:
            s = s.rjust(w)
        out += s
    m.out.extend(out.encode('latin-1'))
    return len(out)


def b_strlen(m, a):
    return len(m.cstr(a[0]))


def b_strcpy(m, a):
    s = m.cstr(a[1]) + b'\0'
    m.mem[a[0]:a[0] + len(s)] = s
    return a[0]


def b_strcmp(m, a):
    x, y = m.cstr(a[0]), m.cstr(a[1])
    return (x > y) - (x < y)


def b_memcpy(m, a):
    m.mem[a[0]:a[0] + a[2]] = m.mem[a[1]:a[1] + a[2]]
    return a[0]


def b_memset(m, a):
    m.mem[a[0]:a[0] + a[2]] = bytes([a[1] & 0xFF]) * a[2]
    return a[0]


def b_exit(m, a):
    raise Halt(a[0])


BUILTINS = {
    'putchar': b_putchar, 'puts': b_puts, 'getchar': b_getchar, 'printf': b_printf, 'strlen': b_strlen, 'strcpy': b_strcpy,
    'strcmp': b_strcmp, 'memcpy': b_memcpy, 'memset': b_memset, 'abs': lambda m, a: abs(s16(a[0])), 'exit': b_exit,
}


def run(text, stdin=b'', maxsteps=5_000_000):
    m = Machine(text, maxsteps, stdin)
    code = 0
    try:
        code = m.call('main', [])
    except Halt as h:
        code = h.code
    return bytes(m.out), code & 0xFF


if __name__ == '__main__':
    path = sys.argv[1]
    out, code = run(open(path).read())
    sys.stdout.buffer.write(out)
    sys.exit(code)
