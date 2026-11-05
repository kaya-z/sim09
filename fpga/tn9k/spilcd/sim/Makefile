# SPDX-License-Identifier: MIT
# Verilator simulation of the SPI screen (ILI9341, 320 x 240, 40 x 15 cells) alone:  make   (CHARSET=0 raw, 1 UTF-8, 2 EUC-JP, 3 Shift_JIS)
#   make run INPUT=spi_test.txt OUT=spi.ppm
# Uses the existing files that the screen shares (../../rtl: hdmi_decode, psram_xfer, psram_arb, font, kanji.vh; ../../lcd/rtl/lcd_term.v); none of them is changed.
CHARSET ?= 1
HALF ?= 1
R   = ../../rtl
SRC = ../rtl/spilcd_text.v ../rtl/spilcd_paint.v ../rtl/spilcd_spi.v ../../lcd/rtl/lcd_term.v $(R)/hdmi_decode.v $(R)/psram_xfer.v $(R)/psram_arb.v ../../sim/psram_model.v ili9341_model.v spilcd_top.v
B   = build/spi$(CHARSET)h$(HALF)
INPUT ?= spi_test.txt
OUT   ?= $(B)/spi.ppm
$(B)/obj/Vspilcd_top: $(SRC) tb_spilcd.cpp $(R)/font8x16.hex $(B)/kanji16.hex
	mkdir -p $(B)
	verilator --cc --exe --build -O2 -j 8 -I$(R) --top-module spilcd_top -Mdir $(B)/obj \
	  -Wno-fatal -Wno-lint -Wno-style -Wno-TIMESCALEMOD $(EXTRA) \
	  -GFONT_FILE='"$(abspath $(R)/font8x16.hex)"' -GKANJI_FILE='"$(abspath $(B)/kanji16.hex)"' -GCHARSET=$(CHARSET) -GSCK_HALF=$(HALF) \
	  -CFLAGS "-O2" $(SRC) $(abspath tb_spilcd.cpp) -o Vspilcd_top >/dev/null
$(B)/kanji16.hex: ../../scripts/mkkanji.py ../../vendor/shinonome/kanji-16.bit
	mkdir -p $(B)
	python3 ../../scripts/mkkanji.py -o $(B)/kanji.bin --hex16 $@ >/dev/null
run: $(B)/obj/Vspilcd_top
	$(B)/obj/Vspilcd_top -i $(INPUT) -o $(OUT)
test: $(B)/obj/Vspilcd_top
	for t in spi_test spi_kanji spi_scroll; do $(B)/obj/Vspilcd_top -i $$t.txt -o $(B)/$$t.ppm || exit 1; done; echo "SPI screen: 3 tests ok (SCK_HALF=$(HALF))"
clean:
	rm -rf build
