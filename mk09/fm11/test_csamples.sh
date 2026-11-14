#!/bin/sh
# SPDX-License-Identifier: MIT
# FM-11 Level 2: every C sample of /C is compiled by cc (inside the simulator) and run; the output is checked.
#   usage: fm11/test_csamples.sh [path-to-fm11-binary]      (make BOARD=fm11 first)
cd "$(dirname "$0")/.."
FM11=${1:-/export/nas/archives/FM-11/emulator/Fm11gtk/fm11}
IMG=build-fm11-l2/mk09.img
W=$(mktemp -d); trap 'rm -rf $W' EXIT; fail=0
SP=$(printf ' %.0s' $(seq 1 80))
t() {   # name, arguments of the program, expected text.  Two sessions on the same disk image: compile, then run (the typed-ahead characters of a long
        # compilation would overflow the input ring of the console)
    cp $IMG $W/d.img; : > $W/t.txt
    "$FM11" --headless 60000 $W/s.ppm -h0 $W/d.img --dipsw 0x7F --keyat 3000 --keygap 14 --keys "cc /C/$1.C -o /$1.MX|" --text $W/t.txt --mute 2>/dev/null
    : > $W/t.txt
    "$FM11" --headless 7000 $W/s.ppm -h0 $W/d.img --dipsw 0x7F --keyat 3000 --keygap 14 --keys "/$1.MX $2|" --text $W/t.txt --mute 2>/dev/null
    grep -q "$3" $W/t.txt && echo "ok   : $1.c" || { echo "FAIL : $1.c  (expected: $3)"; tail -6 $W/t.txt; fail=1; }
}
t hello "a b" "hello from C, 3"
t fib "" "fib(40) = 102334155"
t primes "100" "25 primes up to 100"
t wc "/C/HELLO.C" "/C/HELLO.C"
t fcopy "/C/HELLO.C /H2.C" "bytes copied"
t strings "" "reversed: "
t list "" "orange"
t sort "" "down"
t float "" "pi is about"
[ $fail = 0 ] && echo "C samples: PASS" || { echo "C samples: FAIL"; exit 1; }
