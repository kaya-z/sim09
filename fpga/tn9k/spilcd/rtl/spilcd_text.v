// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The ILI9341 SPI screen (docs/LCD_SPI.md): the terminal of the HDMI / LCD screens (lcd_term.v, COLS x ROWS = 40 x 15 cells) and the decoder (hdmi_decode.v, psram_xfer.v) in front of a
// painter (spilcd_paint.v) that sends the changed cells over SPI.  Everything runs on the pixel clock (33.75 MHz), SCK = 16.875 MHz.
// The video RAM has two copies: one for the painter, one for the scroll operations of the terminal.

module spilcd_text #(
    parameter COLS = 40,
    parameter ROWS = 15,
    parameter CHARSET = 1,
    parameter FONT_FILE = "font8x16.hex",
    parameter [7:0] MADCTL = 8'h28,
    parameter SCK_HALF = 1,
    parameter D_RESET = 130,
    parameter D_SWRESET = 6,
    parameter D_SLPOUT = 130,
    parameter D_DISPON = 6
) (
    input  wire        clk,                 // system clock (the console bytes come in here)
    input  wire        rst,
    input  wire        stb,                 // a byte written to the console
    input  wire [7:0]  din,
    output wire        busy,                // the screen cannot take more characters now: hold them back
    input  wire        kanji_ok,            // the kanji image is in the PSRAM
    // PSRAM word reads (arbiter masters 1 and 2)
    output wire        x1_req,
    output wire [21:0] x1_addr,
    input  wire        x1_ack,
    input  wire [15:0] x1_rdata,
    output wire        x2_req,
    output wire [21:0] x2_addr,
    input  wire        x2_ack,
    input  wire [15:0] x2_rdata,
    input  wire        clk_pix,             // pixel clock 33.75 MHz (everything but the console bytes runs on it)
    input  wire        rst_pix,
    // the panel (ILI9341, 4-wire SPI)
    output wire        lcd_cs,
    output wire        lcd_sck,
    output wire        lcd_mosi,
    output wire        lcd_dc,
    output wire        lcd_rst_n,
    output wire        lcd_started,
    output wire [15:0] cells_painted,
    output wire [7:0]  scr_cols,
    output wire [7:0]  scr_rows
);
    assign scr_cols = COLS;
    assign scr_rows = ROWS;

    // ---------------------------------------------------------------- the console bytes, across the clock domains
    reg        tog = 1'b0;
    reg [7:0]  dreg;
    always @(posedge clk) if (stb) begin tog <= ~tog; dreg <= din; end       // one byte per 87 us at most
    reg [2:0]  ts;
    reg        stb_p;
    reg [7:0]  din_p;
    always @(posedge clk_pix) begin
        ts    <= {ts[1:0], tog};
        stb_p <= (ts[2] != ts[1]);
        din_p <= dreg;
    end

    // ---------------------------------------------------------------- the decoder (pixel clock), PSRAM look-ups through psram_xfer
    wire        dec_busy, sym_stb, sym_wide;
    wire [11:0] sym;
    wire        xp_req, xp_ack;
    wire [21:0] xp_addr;
    wire [15:0] xp_rdata;
    // (declared before its use: a net used in a port connection before its declaration is an implicit net, and the Gowin
    // tool then leaves the later declaration without a driver: kanji_ok_p stayed 0 on the board)
    reg [1:0] kq;
    always @(posedge clk_pix) kq <= {kq[0], kanji_ok};
    wire kanji_ok_p = kq[1];
    hdmi_decode #(.CHARSET(CHARSET)) decode (
        .clk(clk_pix), .rst(rst_pix), .kanji_ok(kanji_ok_p), .stb(stb_p), .din(din_p), .busy(dec_busy),
        .sym_stb(sym_stb), .sym_wide(sym_wide), .sym(sym),
        .x_req(xp_req), .x_addr(xp_addr), .x_ack(xp_ack), .x_rdata(xp_rdata)
    );
    psram_xfer xfer (
        .clk_a(clk_pix), .req_a(xp_req), .addr_a(xp_addr), .ack_a(xp_ack), .rdata_a(xp_rdata),
        .clk_b(clk), .req_b(x1_req), .addr_b(x1_addr), .ack_b(x1_ack), .rdata_b(x1_rdata)
    );

    // the screen cannot take more characters: the decoder or the terminal queue is busy (back to the system clock domain)
    wire busy_p;
    reg [1:0] bs;
    always @(posedge clk) bs <= {bs[0], busy_p | dec_busy};
    reg [4:0] hold = 5'd0;                      // a byte has just come in: busy, until the pixel clock domain has taken it and says so itself
    always @(posedge clk) hold <= stb ? 5'd31 : (hold != 5'd0) ? hold - 1'b1 : 5'd0;
    assign busy = bs[1] | (hold != 5'd0);

    // ---------------------------------------------------------------- video RAM (two copies)
    localparam CELLS = COLS * ROWS;
    wire        vw_en;
    wire [11:0] vw_addr, vr_addr, va;
    wire [23:0] vw_data;
    reg  [23:0] vr_data, vq;
    reg  [23:0] vram_a [0:CELLS-1];
    reg  [23:0] vram_b [0:CELLS-1];
    always @(posedge clk_pix) begin
        if (vw_en && vw_addr < CELLS) begin vram_a[vw_addr] <= vw_data; vram_b[vw_addr] <= vw_data; end
        vr_data <= (vr_addr < CELLS) ? vram_b[vr_addr] : 24'd0;
        vq      <= (va < CELLS) ? vram_a[va] : 24'd0;
    end

    // ---------------------------------------------------------------- font ROM
    reg [7:0]  font [0:4095];
    reg [7:0]  fq;
    wire [11:0] fa;
    initial $readmemh(FONT_FILE, font);
    always @(posedge clk_pix) fq <= font[fa];

    // ---------------------------------------------------------------- terminal and painter
    wire [6:0] cur_col;
    wire [4:0] cur_row;
    lcd_term #(.COLS(COLS), .ROWS(ROWS)) term (
        .clk(clk_pix), .rst(rst_pix), .stb(sym_stb), .din(sym), .wide(sym_wide),
        .vw_en(vw_en), .vw_addr(vw_addr), .vw_data(vw_data), .vr_addr(vr_addr), .vr_data(vr_data),
        .cur_col(cur_col), .cur_row(cur_row), .busy(busy_p)
    );

    wire        xq_req, xq_ack;
    wire [21:0] xq_addr;
    wire [15:0] xq_rdata;
    spilcd_paint #(.COLS(COLS), .ROWS(ROWS), .MADCTL(MADCTL), .SCK_HALF(SCK_HALF), .D_RESET(D_RESET), .D_SWRESET(D_SWRESET), .D_SLPOUT(D_SLPOUT), .D_DISPON(D_DISPON)) paint (
        .clk(clk_pix), .rst(rst_pix), .va(va), .vq(vq), .fa(fa), .fq(fq), .cur_col(cur_col), .cur_row(cur_row),
        .x_req(xq_req), .x_addr(xq_addr), .x_ack(xq_ack), .x_rdata(xq_rdata), .kanji_ok(kanji_ok_p),
        .lcd_cs(lcd_cs), .lcd_rst_n(lcd_rst_n), .lcd_sck(lcd_sck), .lcd_mosi(lcd_mosi), .lcd_dc(lcd_dc),
        .started(lcd_started), .cells_painted(cells_painted)
    );
    psram_xfer xfer2 (
        .clk_a(clk_pix), .req_a(xq_req), .addr_a(xq_addr), .ack_a(xq_ack), .rdata_a(xq_rdata),
        .clk_b(clk), .req_b(x2_req), .addr_b(x2_addr), .ack_b(x2_ack), .rdata_b(x2_rdata)
    );
endmodule
