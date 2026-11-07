// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Test bench top for the LCD text screen alone (lcd/sim/tb_lcd.cpp): console bytes in, the RGB565 panel signals out.  The kanji
// image is in the PSRAM model (preloaded), behind the same arbiter as in the system (the same as sim/hdmi_top.v).
module lcd_top #(
    parameter FONT_FILE = "font8x16.hex",
    parameter KANJI_FILE = "kanji16.hex",
    parameter CHARSET = 0
) (
    input  wire        clk,
    input  wire        clk_pix,
    input  wire        rst,
    input  wire        stb,
    input  wire [7:0]  din,
    output wire        busy,
    output wire [4:0]  lcd_r,
    output wire [5:0]  lcd_g,
    output wire [4:0]  lcd_b,
    output wire        lcd_de,
    output wire        lcd_hs,
    output wire        lcd_vs,
    output wire [7:0]  scr_cols,
    output wire [7:0]  scr_rows
);
    reg [3:0] rp = 4'hf;
    always @(posedge clk_pix) rp <= {rp[2:0], rst};

    wire        x1_req, x1_ack, x2_req, x2_ack;
    wire [21:0] x1_addr, x2_addr;
    wire [15:0] x1_rdata, x2_rdata;
    lcd_text #(.CHARSET(CHARSET), .FONT_FILE(FONT_FILE)) t (
        .clk(clk), .rst(rst), .stb(stb), .din(din), .busy(busy), .kanji_ok(1'b1),
        .x1_req(x1_req), .x1_addr(x1_addr), .x1_ack(x1_ack), .x1_rdata(x1_rdata),
        .x2_req(x2_req), .x2_addr(x2_addr), .x2_ack(x2_ack), .x2_rdata(x2_rdata),
        .clk_pix(clk_pix), .rst_pix(rp[3]),
        .lcd_r(lcd_r), .lcd_g(lcd_g), .lcd_b(lcd_b), .lcd_de(lcd_de), .lcd_hs(lcd_hs), .lcd_vs(lcd_vs),
        .scr_cols(scr_cols), .scr_rows(scr_rows)
    );

    wire        ps_rd, ps_wr, ps_bw, ps_busy;
    wire [21:0] ps_addr;
    wire [15:0] ps_din, ps_dout;
    psram_arb arb (
        .clk(clk), .rst(rst),
        .c_rd(ps_rd), .c_wr(ps_wr), .c_addr(ps_addr), .c_din(ps_din), .c_bw(ps_bw), .c_dout(ps_dout), .c_busy(ps_busy),
        .m0_req(1'b0), .m0_wr(1'b0), .m0_addr(22'd0), .m0_din(8'd0), .m0_ack(), .m0_rdata(),
        .m1_req(x1_req), .m1_addr(x1_addr), .m1_ack(x1_ack), .m1_rdata(x1_rdata),
        .m2_req(x2_req), .m2_addr(x2_addr), .m2_ack(x2_ack), .m2_rdata(x2_rdata),
        .m3_req(1'b0), .m3_wr(1'b0), .m3_addr(22'd0), .m3_din(8'd0), .m3_ack(), .m3_rdata()
    );
    PsramModel #(.INIT_FILE(KANJI_FILE), .INIT_BASE(21'h100000)) mem (
        .clk(clk), .resetn(~rst), .read(ps_rd), .write(ps_wr), .addr(ps_addr), .din(ps_din),
        .byte_write(ps_bw), .dout(ps_dout), .busy(ps_busy)
    );
endmodule
