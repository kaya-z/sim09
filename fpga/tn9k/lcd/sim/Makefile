# SPDX-License-Identifier: MIT
# Verilator simulation of the LCD text screen alone (800 x 480, 100 x 30 cells):  make   (CHARSET=0 raw, 1 UTF-8, 2 EUC-JP, 3 Shift_JIS)
#   make run INPUT=lcd_test.txt OUT=lcd.ppm
# Uses the existing files of the HDMI screen that the LCD screen shares (../../rtl: hdmi_decode, psram_xfer, psram_arb, font, kanji.vh); none of them is changed.
CHARSET ?= 1
R   = ../../rtl
SRC = ../rtl/lcd_text.v ../rtl/lcd_term.v ../rtl/lcd_video.v ../rtl/lcd_glyph.v $(R)/hdmi_decode.v $(R)/psram_xfer.v $(R)/psram_arb.v ../../sim/psram_model.v lcd_top.v
B   = build/lcd$(CHARSET)
INPUT ?= lcd_test.txt
OUT   ?= $(B)/lcd.ppm
$(B)/obj/Vlcd_top: $(SRC) tb_lcd.cpp $(R)/font8x16.hex $(B)/kanji16.hex
	mkdir -p $(B)
	verilator --cc --exe --build -O2 -j 8 -I$(R) --top-module lcd_top -Mdir $(B)/obj \
	  -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD \
	  -GFONT_FILE='"$(abspath $(R)/font8x16.hex)"' -GKANJI_FILE='"$(abspath $(B)/kanji16.hex)"' -GCHARSET=$(CHARSET) \
	  -CFLAGS "-O2" $(SRC) $(abspath tb_lcd.cpp) -o Vlcd_top >/dev/null
$(B)/kanji16.hex: ../../scripts/mkkanji.py ../../vendor/shinonome/kanji-16.bit
	mkdir -p $(B)
	python3 ../../scripts/mkkanji.py -o $(B)/kanji.bin --hex16 $@ >/dev/null
run: $(B)/obj/Vlcd_top
	$(B)/obj/Vlcd_top -i $(INPUT) -o $(OUT)
test: $(B)/obj/Vlcd_top
	for t in lcd_test lcd_kanji lcd_scroll; do $(B)/obj/Vlcd_top -i $$t.txt -o $(B)/$$t.ppm -f 2 || exit 1; done; echo LCD screen: 3 tests ok
clean:
	rm -rf build
