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
    "$FM11" --headless "$(( $4 + LGF ))" $W/s.ppm $2 $W/d.img --dipsw "$3" --keyat 3000 --keygap 14 --keys "$LG$5" --text $W/t.txt --mute 2>/dev/null
}
# the file names: Level 1 has the upper case names of 1.x, Level 2 is the 2.0 system with case sensitive (lower case) names
nm() { if [ "$L" = 2 ]; then echo "$*" | tr 'A-Z' 'a-z'; else echo "$*"; fi; }
for L in $LEVELS; do
    LG=""; LGF=0                                        # Level 2 (2.0) starts with the login: root, the password root
    if [ $L = 2 ]; then LG="root|root|"; LGF=800; fi
    # Level 2 (2.0): the images without the "must change" marks of the passwords (make BOARD=fm11 testimg; make BOARD=fm11 BOOTDEV=2hd testimg)
    T=""; if [ $L = 2 ]; then T="_t"; fi
    HD=build-fm11-l$L/mk09$T.img; FD=build-fm11-l$L-2hd/mk09_2hd$T.img
    echo "---- Level $L"
    run $HD -h0 0x7F 6500 "$(nm cp ETC/RC.SH T.TXT)|$(nm cat T.TXT)|df|"
    check "for FM-11" "L$L hard disk: MK09 boots, the version says for FM-11"
    check "Level $L" "L$L hard disk: the banner names the level"
    check "stty sjis" "L$L hard disk: a file is copied and read back"
    check "8167 clusters" "L$L hard disk: df sees the volume"
    run $FD -f0 0xBF 6500 "$(nm cp ETC/RC.SH T.TXT)|$(nm cat T.TXT)|df|"
    check "for FM-11" "L$L 2HD: MK09 boots"
    check "stty sjis" "L$L 2HD: a file is copied and read back"
    check "47[2-6] clusters" "L$L 2HD: df sees the volume"
    run $HD -h0 0x7F 11000 "reboot|${SP}${LG}mem|"
    if [ $L = 1 ]; then check "pages of 256 bytes" "L$L hard disk: reboot comes back, the shell answers"
    else check "physical memory" "L$L hard disk: reboot comes back, the shell answers (mem shows the physical memory)"; fi
    run $HD -h0 0x7F 7000 "date|date -s 2028-02-28 23:59:58|date|date -s 2026-13-01|"
    check "2026-01-01 00:0" "L$L date: the clock of the system starts at 2026-01-01"
    check "2028-02-28 23:59:5" "L$L date -s: the clock is set"
    check "bad date or time" "L$L date -s: a bad date is refused"
    run $HD -h0 0x7F 16000 "date -s 2028-12-31 23:59:58|dhry 400|${SP20}date|"       # (dhry runs a second or two: the clock goes on over the end of the year)
    check "2029-01-01 00:00:" "L$L date: the clock runs on (the end of the year, the calendar)"
    if [ $L = 2 ]; then   # 2.0: the names are case sensitive, two files that differ only in the case live together
        run $HD -h0 0x7F 7000 "cp etc/rc.sh Aa.txt|cp etc/rc.sh aA.txt|ls|"
        check "Aa.txt" "L2 names: Aa.txt is made"
        check "aA.txt" "L2 names: aA.txt is a different file"
        # 2.0: users (PM keeps the user of every task, FS checks the permissions): root, then user 100 group 10 through runas
        run $HD -h0 0x7F 9000 "id|mkdir /pub|chmod 700 /pub|runas 100 10 id|runas 100 10 ls /pub|ls /pub|"
        check "uid=0(root) gid=0(root)" "L2 users: the shell is root"
        check "uid=100(user) gid=10(10)" "L2 users: runas changes the user of the program"
        check "not allowed" "L2 users: user 100 cannot read the directory of root (700)"
        run $HD -h0 0x7F 9000 "chmod 4755 /bin/id.mx|runas 100 10 id|chmod 700 /bin/id.mx|runas 100 10 id|"
        check "uid=0(root) gid=10(10)" "L2 users: a setuid root file runs as root"
        check "runas: not allowed" "L2 users: a file of root with 700 cannot be run by another user (said by the shell as not allowed)"
        # 2.0: login, passwords, su, passwd, ls -l (the typing is slow: a login and a hash take about a second)
        LG=""
        run $HD -h0 0x7F 2500 "root|wrong|"
        check "Login incorrect" "L2 login: a wrong password is refused"
        run $HD -h0 0x7F 5000 "root|wrong|user|user|id|pwd|ls -l /etc|"
        check "uid=100(user) gid=100(users)" "L2 login: user logs in, id shows the names"
        check "/home/user>" "L2 login: the shell of user starts in the home directory"
        check "rw------- root" "L2 ls -l: shadow is 600 and root's"
        run $HD -h0 0x7F 5000 "user|user|su|bad|su|root|id|"
        check "su: wrong password" "L2 su: a wrong password is refused"
        check "uid=0(root) gid=0(root)" "L2 su: root with the right password"
        run $HD -h0 0x7F 5000 "user|user|date -s 2030-01-01 00:00:00|date|"
        check "cannot be set" "L2 root only: user cannot set the clock"
        nocheck "2030-01-01" "L2 root only: the clock was not changed"
        run $HD -h0 0x7F 9000 "root|root|passwd user|nu|nu|exit|user|nu|id|"
        check "password changed" "L2 passwd: root sets the password of user"
        check "uid=100(user)" "L2 passwd: user logs in with the new password"
        LG="root|root|"
    fi
    run $HD -h0 0x7F 7000 "basic|PRINT 6*7|"
    check " 42" "L$L basic runs"
    if [ $L = 2 ]; then   # the installed RAM is found at the start (the simulator: --mem KB)
        for M in 128 512 1024; do
            cp $HD $W/d.img; : > $W/t.txt
            "$FM11" --headless $((16000 + LGF)) $W/s.ppm -h0 $W/d.img --dipsw 0x7F --mem $M --keyat 7000 --keygap 14 --keys "${SP}${LG}mem|" --text $W/t.txt --mute 2>/dev/null
            check "RAM ${M}KB = $((M / 4)) pages" "L2 --mem $M: the kernel finds the RAM, mem shows it"
        done
    fi
    if [ $L = 1 ]; then
        run $HD -h0 0x7F 5000 "cc|"
        check "not found" "L1: cc (it needs cc1: 160 pages) is not in the image"
    else
        run $HD -h0 0x7F 60000 "$(nm cc ASM/HELLO.C)|${SP60}$(nm ASM/HELLO)|"
        check "hello from C" "L2: the C compiler compiles a program (cc1: 160 pages) and it runs"
    fi
    # format + mount: a blank floppy in the 2HD drive (the system drive 0 is the hard disk, the floppy is drive 1)
    cp $HD $W/d.img; : > $W/blank.d88; : > $W/t.txt
    "$FM11" --headless $((60000 + LGF)) $W/s.ppm -h0 $W/d.img -f0 $W/blank.d88 --dipsw 0x7F --keyat 3000 --keygap 40 \
        --keys "${LG}format -y 1 T|${SP60}mount 1|${SP20}$(nm cp ETC/RC.SH 1:/R.TXT)|${SP20}$(nm mkdir 1:/D)|${SP20}$(nm mv 1:/R.TXT 1:/D/R2.TXT)|${SP20}$(nm ls 1:/D)|${SP20}df 1|${SP20}umount 1|" --text $W/t.txt --mute 2>/dev/null
    check "973 clusters, 971 free" "L$L format + mount + cp + mkdir: the floppy is formatted, files and a directory are written (df 1)"
    check "$(nm R2.TXT)" "L$L mount: mkdir and mv on the floppy, ls shows the file"
    check "drive 1 is free" "L$L umount: the drive is released"
done
[ $fail = 0 ] && echo "fm11 test: PASS" || { echo "fm11 test: FAIL"; exit 1; }
