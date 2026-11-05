// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// Test bench top for the SPI screen alone (spilcd/sim/tb_spilcd.cpp): console bytes in, the ILI9341 model behind the SPI pins.  The kanji image is in the PSRAM model
// (preloaded) behind the same arbiter as in the system (as sim/hdmi_top.v).
module spilcd_top #(
    parameter FONT_FILE = "font8x16.hex",
    parameter KANJI_FILE = "kanji16.hex",
    parameter CHARSET = 0,
    parameter [7:0] MADCTL = 8'h28,
    parameter SCK_HALF = 1,
    parameter D_RESET = 130,
    parameter D_SLPOUT = 130
) (
    input  wire        clk,
    input  wire        clk_pix,
    input  wire        rst,
    input  wire        stb,
    input  wire [7:0]  din,
    output wire        busy,
    output wire        started,
    output wire [15:0] cells_painted,
    input  wire [16:0] gram_a,
    output wire [15:0] gram_q,
    output wire [31:0] n_cmd, n_pix, fast, bad_setup, bad_early, bad_format, bad_state,
    output wire        disp_on,
    output wire [7:0]  madctl, colmod,
    output wire [55:0] seq,
    output wire [7:0]  scr_cols,
    output wire [7:0]  scr_rows
);
    reg [3:0] rp = 4'hf;
    always @(posedge clk_pix) rp <= {rp[2:0], rst};

    wire        x1_req, x1_ack, x2_req, x2_ack;
    wire [21:0] x1_addr, x2_addr;
    wire [15:0] x1_rdata, x2_rdata;
    wire        cs, sck, mosi, dc, rst_n;
    spilcd_text #(.CHARSET(CHARSET), .FONT_FILE(FONT_FILE), .MADCTL(MADCTL), .SCK_HALF(SCK_HALF), .D_RESET(D_RESET), .D_SLPOUT(D_SLPOUT)) t (
        .clk(clk), .rst(rst), .stb(stb), .din(din), .busy(busy), .kanji_ok(1'b1),
        .x1_req(x1_req), .x1_addr(x1_addr), .x1_ack(x1_ack), .x1_rdata(x1_rdata),
        .x2_req(x2_req), .x2_addr(x2_addr), .x2_ack(x2_ack), .x2_rdata(x2_rdata),
        .clk_pix(clk_pix), .rst_pix(rp[3]),
        .lcd_cs(cs), .lcd_sck(sck), .lcd_mosi(mosi), .lcd_dc(dc), .lcd_rst_n(rst_n), .lcd_started(started), .cells_painted(cells_painted),
        .scr_cols(scr_cols), .scr_rows(scr_rows)
    );
    ili9341_model lcd (
        .clk(clk_pix), .rst_n(rst_n), .cs_n(cs), .sck(sck), .mosi(mosi), .dc(dc), .gram_a(gram_a), .gram_q(gram_q),
        .n_cmd(n_cmd), .n_pix(n_pix), .disp_on(disp_on), .sleeping(), .fast(fast), .bad_setup(bad_setup), .bad_early(bad_early),
        .bad_format(bad_format), .bad_state(bad_state), .madctl(madctl), .colmod(colmod), .seq(seq)
    );

    wire        ps_rd, ps_wr, ps_bw, ps_busy;
    wire [21:0] ps_addr;
    wire [15:0] ps_din, ps_dout;
    psram_arb arb (
        .clk(clk), .rst(rst),
        .c_rd(ps_rd), .c_wr(ps_wr), .c_addr(ps_addr), .c_din(ps_din), .c_bw(ps_bw), .c_dout(ps_dout), .c_busy(ps_busy),
        .m0_req(1'b0), .m0_wr(1'b0), .m0_addr(22'd0), .m0_din(8'd0), .m0_ack(), .m0_rdata(),
        .m1_req(x1_req), .m1_addr(x1_addr), .m1_ack(x1_ack), .m1_rdata(x1_rdata),
        .m2_req(x2_req), .m2_addr(x2_addr), .m2_ack(x2_ack), .m2_rdata(x2_rdata)
    );
    PsramModel #(.INIT_FILE(KANJI_FILE), .INIT_BASE(21'h100000)) mem (
        .clk(clk), .resetn(~rst), .read(ps_rd), .write(ps_wr), .addr(ps_addr), .din(ps_din),
        .byte_write(ps_bw), .dout(ps_dout), .busy(ps_busy)
    );
endmodule
