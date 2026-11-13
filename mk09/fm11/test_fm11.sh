#!/bin/sh
# SPDX-License-Identifier: MIT
# FM-11 regression test (headless, with the Linux simulator emulator/Fm11gtk):
#   - the hard disk (MDC) image and the 2HD (MB8877) image boot, the shell answers the keyboard,
#   - a file is copied (disk write + read), the reboot command restarts the system from the disk.
#   usage: fm11/test_fm11.sh [path-to-fm11-binary]   (run make BOARD=fm11 and make BOARD=fm11 BOOTDEV=2hd first)
cd "$(dirname "$0")/.."
FM11=${1:-/export/nas/archives/FM-11/emulator/Fm11gtk/fm11}
W=$(mktemp -d); trap 'rm -rf $W' EXIT; fail=0
SP=$(printf ' %.0s' $(seq 1 90)); SP40=$(printf ' %.0s' $(seq 1 40)); SP30=$(printf ' %.0s' $(seq 1 30)); SP20=$(printf ' %.0s' $(seq 1 20)); SP60=$(printf ' %.0s' $(seq 1 100))
check() { grep -q "$1" $W/t.txt && echo "ok   : $2" || { echo "FAIL : $2"; fail=1; }; }
run() {   # image, drive option, dipsw, frames, keys
    cp "$1" $W/d.img; : > $W/t.txt
    "$FM11" --headless "$4" $W/s.ppm $2 $W/d.img --dipsw "$3" --keyat 3000 --keygap 14 --keys "$5" --text $W/t.txt --mute 2>/dev/null
}
run build-fm11/mk09.img -h0 0x7F 6500 "cp ETC/RC.SH T.TXT|cat T.TXT|df|"
check "MK09 1.36" "hard disk: MK09 boots"
check "stty sjis" "hard disk: a file is copied and read back"
check "8167 clusters" "hard disk: df sees the volume"
run build-fm11-2hd/mk09_2hd.img -f0 0xBF 6500 "cp ETC/RC.SH T.TXT|cat T.TXT|df|"
check "MK09 1.36" "2HD: MK09 boots"
check "stty sjis" "2HD: a file is copied and read back"
check "476 clusters" "2HD: df sees the volume"
run build-fm11/mk09.img -h0 0x7F 11000 "reboot|${SP}mem|"
check "pages of 256 bytes" "hard disk: reboot comes back, the shell answers"
# format + mount: a blank floppy in the 2HD drive (the system drive 0 is the hard disk, the floppy is drive 1): copy a file to it, read it back
cp build-fm11/mk09.img $W/d.img; : > $W/blank.d88; : > $W/t.txt
"$FM11" --headless 60000 $W/s.ppm -h0 $W/d.img -f0 $W/blank.d88 --dipsw 0x7F --keyat 3000 --keygap 40 \
    --keys "format -y 1 T|${SP60}mount 1|${SP20}cp ETC/RC.SH 1:/R.TXT|${SP20}mkdir 1:/D|${SP20}mv 1:/R.TXT 1:/D/R2.TXT|${SP20}ls 1:/D|${SP20}df 1|${SP20}umount 1|" --text $W/t.txt --mute 2>/dev/null
check "973 clusters, 971 free" "format + mount + cp + mkdir: the floppy is formatted, files and a directory are written (df 1)"
check "R2.TXT" "mount: mkdir and mv on the floppy, ls shows the file"
check "drive 1 is free" "umount: the drive is released"
[ $fail = 0 ] && echo "fm11 test: PASS" || { echo "fm11 test: FAIL"; exit 1; }
