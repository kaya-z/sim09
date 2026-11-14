#!/bin/sh
# SPDX-License-Identifier: MIT
# FM-11 regression test (headless, with the Linux simulator emulator/Fm11gtk), for Level 1 (no MMU) and Level 2 (MMU):
#   - the hard disk (MDC) image and the 2HD (MB8877) image boot, the shell answers the keyboard,
#   - a file is copied (disk write + read), the reboot command restarts the system from the disk,
#   - date: the clock of the system (kept by the kernel) starts at 2026-01-01, date -s sets it, it runs on over a leap day and a year end,
#   - format, mount, cp, mkdir, mv, ls, df, umount on a floppy disk in drive 1,
#   - Level 2: the C compiler (cc1 needs 160 pages) compiles a program and the program runs; Level 1: the programs that do not fit are not in the image.
#   usage: fm11/test_fm11.sh [path-to-fm11-binary] [levels]    (default: "1 2"; make BOARD=fm11 LEVEL=n and make BOARD=fm11 LEVEL=n BOOTDEV=2hd first)
cd "$(dirname "$0")/.."
FM11=${1:-/export/nas/archives/FM-11/emulator/Fm11gtk/fm11}
LEVELS=${2:-"1 2"}
W=$(mktemp -d); trap 'rm -rf $W' EXIT; fail=0
sp() { printf ' %.0s' $(seq 1 "$1"); }
SP=$(sp 90); SP20=$(sp 20); SP60=$(sp 100); SP100=$(sp 120)
check() { grep -q "$1" $W/t.txt && echo "ok   : $2" || { echo "FAIL : $2"; fail=1; }; }
nocheck() { grep -q "$1" $W/t.txt && { echo "FAIL : $2"; fail=1; } || echo "ok   : $2"; }
run() {   # image, drive option, dipsw, frames, keys
    cp "$1" $W/d.img; : > $W/t.txt
    "$FM11" --headless "$4" $W/s.ppm $2 $W/d.img --dipsw "$3" --keyat 3000 --keygap 14 --keys "$5" --text $W/t.txt --mute 2>/dev/null
}
for L in $LEVELS; do
    HD=build-fm11-l$L/mk09.img; FD=build-fm11-l$L-2hd/mk09_2hd.img
    echo "---- Level $L"
    run $HD -h0 0x7F 6500 "cp ETC/RC.SH T.TXT|cat T.TXT|df|"
    check "for FM-11" "L$L hard disk: MK09 boots, the version says for FM-11"
    check "Level $L" "L$L hard disk: the banner names the level"
    check "stty sjis" "L$L hard disk: a file is copied and read back"
    check "8167 clusters" "L$L hard disk: df sees the volume"
    run $FD -f0 0xBF 6500 "cp ETC/RC.SH T.TXT|cat T.TXT|df|"
    check "for FM-11" "L$L 2HD: MK09 boots"
    check "stty sjis" "L$L 2HD: a file is copied and read back"
    check "47[46] clusters" "L$L 2HD: df sees the volume"
    run $HD -h0 0x7F 11000 "reboot|${SP}mem|"
    if [ $L = 1 ]; then check "pages of 256 bytes" "L$L hard disk: reboot comes back, the shell answers"
    else check "physical memory" "L$L hard disk: reboot comes back, the shell answers (mem shows the physical memory)"; fi
    run $HD -h0 0x7F 9000 "date|date -s 2028-02-28 23:59:58|date|date -s 2028-12-31 23:59:50|${SP20}${SP20}date|date -s 2026-13-01|"
    check "2026-01-01 00:0" "L$L date: the clock of the system starts at 2026-01-01"
    check "2028-02-28 23:59:5" "L$L date -s: the clock is set"
    check "2029-01-01 00:00:" "L$L date: the clock runs on (the end of the year, the calendar)"
    check "bad date or time" "L$L date -s: a bad date is refused"
    run $HD -h0 0x7F 7000 "basic|PRINT 6*7|"
    check " 42" "L$L basic runs"
    if [ $L = 1 ]; then
        run $HD -h0 0x7F 5000 "cc|"
        check "not found" "L1: cc (it needs cc1: 160 pages) is not in the image"
    else
        run $HD -h0 0x7F 60000 "cc ASM/HELLO.C|${SP60}ASM/HELLO|"
        check "hello from C" "L2: the C compiler compiles a program (cc1: 160 pages) and it runs"
    fi
    # format + mount: a blank floppy in the 2HD drive (the system drive 0 is the hard disk, the floppy is drive 1)
    cp $HD $W/d.img; : > $W/blank.d88; : > $W/t.txt
    "$FM11" --headless 60000 $W/s.ppm -h0 $W/d.img -f0 $W/blank.d88 --dipsw 0x7F --keyat 3000 --keygap 40 \
        --keys "format -y 1 T|${SP60}mount 1|${SP20}cp ETC/RC.SH 1:/R.TXT|${SP20}mkdir 1:/D|${SP20}mv 1:/R.TXT 1:/D/R2.TXT|${SP20}ls 1:/D|${SP20}df 1|${SP20}umount 1|" --text $W/t.txt --mute 2>/dev/null
    check "973 clusters, 971 free" "L$L format + mount + cp + mkdir: the floppy is formatted, files and a directory are written (df 1)"
    check "R2.TXT" "L$L mount: mkdir and mv on the floppy, ls shows the file"
    check "drive 1 is free" "L$L umount: the drive is released"
done
[ $fail = 0 ] && echo "fm11 test: PASS" || { echo "fm11 test: FAIL"; exit 1; }
