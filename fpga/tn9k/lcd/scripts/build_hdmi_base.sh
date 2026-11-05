#!/bin/sh
# SPDX-License-Identifier: MIT
# Build the HDMI version for comparison with build_lcd.sh (the same sources of git HEAD, in lcd/build/hdmi_base), so that the numbers of the two are alike.
# (The LCD version, docs/LCD.md) with the Gowin IDE, for the resource use and the timing margin (an estimate: the bitstream is not meant for the board yet).
#
#   fpga/tn9k/lcd/scripts/build_lcd.sh [IDE_DIR]          (the FUZIX boot ROM, like  build.sh fuzix)
#
# The existing files are only read.  sbc09_soc.v is taken from git (HEAD), so that work in progress on it does not enter the numbers.
# Result: fpga/tn9k/lcd/build/hdmi_base/ (build.log, impl/pnr/*.rpt.txt, sbc09_hdmi_base.fs)
set -e
HERE=$(cd "$(dirname "$0")/../.." && pwd)            # fpga/tn9k
ROOT=$(cd "$HERE/../.." && pwd)
LCD=$HERE/lcd
IDE=${1:-${GOWIN_IDE:-/usr/local/IDE}}
B=$LCD/build/hdmi_base
mkdir -p "$B"
python3 "$HERE/scripts/mkfont.py" -o "$B/font8x16.hex" > /dev/null
python3 "$HERE/scripts/mkkanji.py" -o "$B/kanji.bin" > /dev/null
python3 "$HERE/scripts/bin2hex.py" "$ROOT/fuzix/images/bootrom.bin" > "$B/rom.hex"
git -C "$ROOT" show HEAD:fpga/tn9k/rtl/sbc09_soc.v > "$B/sbc09_soc.v"
set -- $(date +"%Y %m %d %H %M %S")
cat > "$B/tn9k_cfg.vh" <<EOF
\`define CLK_HZ 67500000
\`define PH_DIV 9
\`define ROM_BASE 16'hFF00
\`define ROM_AW 8
\`define FREERUN 0
\`define RTC_YEAR $(( $1 - 1900 ))
\`define RTC_MONTH $(echo $2 | sed 's/^0//')
\`define RTC_DAY $(echo $3 | sed 's/^0//')
\`define RTC_HOUR $(echo $4 | sed 's/^0//')
\`define RTC_MIN $(echo $5 | sed 's/^0//')
\`define RTC_SEC $(echo $6 | sed 's/^0//')
\`define HDMI_ROWS 25
\`define HDMI_CHARSET 1
\`define KANJI_LOAD 1
\`define HDMI 1
EOF
cat "$HERE/constraints/tn9k.cst" "$HERE/constraints/hdmi.cst" > "$B/pins.cst"
cat "$HERE/constraints/tn9k.sdc" "$HERE/constraints/hdmi.sdc" > "$B/timing.sdc"
cat > "$B/build.tcl" <<EOF
set_device -name GW1NR-9C GW1NR-LV9QN88PC6/I5
add_file -type verilog $HERE/rtl/tn9k_top.v
add_file -type verilog $HERE/rtl/tn9k_pll.v
add_file -type verilog $B/sbc09_soc.v
add_file -type verilog $HERE/rtl/acia_uart.v
add_file -type verilog $HERE/rtl/timer_rtc.v
add_file -type verilog $HERE/rtl/sd_disk.v
add_file -type verilog $HERE/rtl/psram_arb.v
add_file -type verilog $HERE/rtl/dbg_uart.v
add_file -type verilog $HERE/rtl/kanji_loader.v
add_file -type verilog $HERE/rtl/hdmi_decode.v
add_file -type verilog $HERE/rtl/hdmi_text.v
add_file -type verilog $HERE/rtl/hdmi_term.v
add_file -type verilog $HERE/rtl/hdmi_video.v
add_file -type verilog $HERE/rtl/hdmi_phy.v
add_file -type verilog $HERE/rtl/hdmi_glyph.v
add_file -type verilog $HERE/rtl/tmds_enc.v
add_file -type verilog $HERE/rtl/psram_xfer.v
add_file -type verilog $HERE/vendor/mc6809/mc6809i.v
add_file -type verilog $HERE/vendor/psram/psram_controller.v
add_file -type cst $B/pins.cst
add_file -type sdc $B/timing.sdc
set_option -top_module tn9k_top
set_option -verilog_std sysv2017
set_option -include_path "$B;$HERE/rtl"
set_option -output_base_name sbc09_hdmi_base
set_option -use_sspi_as_gpio 1
set_option -use_mspi_as_gpio 1
set_option -use_ready_as_gpio 1
set_option -use_done_as_gpio 1
set_option -use_cpu_as_gpio 1
set_option -rw_check_on_ram 1
set_option -place_option 1
run all
exit
EOF
cd "$B"
env -u DISPLAY QT_QPA_PLATFORM=offscreen "$IDE/bin/gw_sh" build.tcl 2>&1 | tee build.log | grep -v "^qt\."
if grep -E "Undeclared symbol|does not have a driver" build.log | grep -v "O_psram_ck_n\|psram_controller" ; then echo "WARNING: see the lines above (build.log)"; fi
ls -l "$B/impl/pnr/"*.fs
