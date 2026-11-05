// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The painter of the ILI9341 screen (docs/LCD_SPI.md).  The screen of the terminal (a video RAM of COLS x ROWS cells) is shown on the 320 x 240 panel (landscape)
// whose frame memory is written over SPI.  Nothing is refreshed: the painter walks over the cells all the time, and a cell that differs from what was painted last
// (a shadow copy of the word that was drawn, with the cursor flag) is sent: a window of 8 x 16 pixels (CASET, PASET) and 128 pixels of RGB565.  So a scroll
// is a redraw of the cells that changed, and a screen that does not change costs no traffic (apart from the blinking cursor).
//
// Start: RESET low, wait, then  01h (software reset), 11h (sleep out, 120 ms), 3Ah = 55h (16 bits per pixel), 36h = MADCTL (landscape), 29h (display on).
// The glyph of a cell comes from the font ROM (8 x 16) or, for a kanji (the two halves of a wide character), from the kanji image in the PSRAM
// (a word of 16 dots per line: the left half is the low byte, the right half the high byte).

`include "kanji.vh"

module spilcd_paint #(
    parameter COLS = 40,
    parameter ROWS = 15,
    parameter [7:0] MADCTL = 8'h28,         // MV | BGR: landscape (the usual setting; the orientation depends on the module)
    parameter SCK_HALF = 1,                 // clocks per half period of SCK (spilcd_spi.v)
    parameter T_RESET = 34000,              // RESET low: 1 ms at 33.75 MHz
    parameter T_UNIT  = 32768,              // the unit of the delays of the start sequence: 0.97 ms (the delays use a shift by 15: keep it 2**15)
    parameter D_RESET = 130,                // after RESET: 126 ms
    parameter D_SWRESET = 6,                // after 01h: 5.8 ms
    parameter D_SLPOUT = 130,               // after 11h: 126 ms (the datasheet: 120 ms)
    parameter D_DISPON = 6
) (
    input  wire        clk,                 // pixel clock
    input  wire        rst,
    // the video RAM, read side (one clock of latency), and the font ROM
    output reg  [11:0] va,
    input  wire [23:0] vq,
    output reg  [11:0] fa,
    input  wire [7:0]  fq,
    input  wire [6:0]  cur_col,
    input  wire [4:0]  cur_row,
    // the kanji image (psram_xfer, side A)
    output reg         x_req,
    output reg  [21:0] x_addr,
    input  wire        x_ack,
    input  wire [15:0] x_rdata,
    input  wire        kanji_ok,
    // the panel
    output reg         lcd_cs,
    output reg         lcd_rst_n,
    output wire        lcd_sck,
    output wire        lcd_mosi,
    output wire        lcd_dc,
    output wire        started,             // the start sequence is done
    output reg  [15:0] cells_painted        // (for the test bench) the number of cells sent
);
    localparam CELLS = COLS * ROWS;

    // ---------------------------------------------------------------- the SPI master
    reg        put, put_dc;
    reg  [7:0] put_byte;
    wire       ready, idle;
    spilcd_spi #(.HALF(SCK_HALF)) spi (.clk(clk), .rst(rst), .put(put), .put_dc(put_dc), .put_byte(put_byte), .ready(ready), .idle(idle),
                    .sck(lcd_sck), .mosi(lcd_mosi), .dc(lcd_dc));

    // ---------------------------------------------------------------- the shadow copy: what the panel shows (25 bits: the cursor flag and the video RAM word)
    reg [24:0] shadow [0:CELLS-1];
    reg [24:0] sq;
    reg        sw;
    reg [11:0] sa;
    reg [24:0] sd;
    always @(posedge clk) begin
        if (sw) shadow[sa] <= sd;
        sq <= shadow[sa];
    end

    // ---------------------------------------------------------------- the blinking cursor
    reg [24:0] blink_cnt;
    wire       blink = blink_cnt[24];
    always @(posedge clk) blink_cnt <= rst ? 25'd0 : blink_cnt + 1'b1;

    // ---------------------------------------------------------------- the colours: the 16 ANSI colours as RGB565
    function [15:0] palette;
        input [3:0] k;
        reg [7:0] on, off, r, g, b;
        begin
            on  = k[3] ? 8'hff : 8'haa;
            off = k[3] ? 8'h55 : 8'h00;
            r = k[0] ? on : off; g = k[1] ? on : off; b = k[2] ? on : off;
            palette = {r[7:3], g[7:2], b[7:3]};
        end
    endfunction

    // ---------------------------------------------------------------- the start sequence (a ROM of steps: type, value)
    //   type 0 = command byte, 1 = data byte, 2 = delay of 'value' units, 3 = end
    function [9:0] initrom;
        input [3:0] i;
        case (i)
        4'd0:  initrom = {2'd0, 8'h01};
        4'd1:  initrom = {2'd2, D_SWRESET[7:0]};
        4'd2:  initrom = {2'd0, 8'h11};
        4'd3:  initrom = {2'd2, D_SLPOUT[7:0]};
        4'd4:  initrom = {2'd0, 8'h3a};
        4'd5:  initrom = {2'd1, 8'h55};
        4'd6:  initrom = {2'd0, 8'h36};
        4'd7:  initrom = {2'd1, MADCTL};
        4'd8:  initrom = {2'd0, 8'h29};
        4'd9:  initrom = {2'd2, D_DISPON[7:0]};
        default: initrom = {2'd3, 8'h00};
        endcase
    endfunction

    // ---------------------------------------------------------------- the state machine
    localparam [4:0]
        S_RST0 = 5'd0,  S_RST1 = 5'd1,  S_INIT = 5'd2,  S_ISEND = 5'd3, S_IDLY = 5'd4,  S_CLR = 5'd5,
        S_SCAN0 = 5'd6, S_SCAN1 = 5'd7, S_SCAN2 = 5'd8, S_CMP = 5'd9,
        S_CS = 5'd10,   S_HDR = 5'd11,  S_LINE = 5'd12, S_GET = 5'd13,  S_GETW = 5'd14, S_PIX = 5'd15,
        S_END = 5'd16,  S_SHADOW = 5'd17;
    reg [4:0]  st;
    reg [22:0] tcnt;                        // up to 130 * 32768 = 4.26 M clocks
    reg [3:0]  ii;
    wire [9:0] ir = initrom(ii);
    reg [9:0]  step;
    reg [11:0] idx;                         // the cell
    reg [6:0]  col;
    reg [4:0]  row;
    reg [24:0] cw;                          // the word that is painted: {cursor flag, video RAM word}
    reg [3:0]  hi;                          // the header byte number
    reg [4:0]  line;                        // 0..16
    reg [7:0]  gbits;
    reg [3:0]  pb;                          // the pixel in the line
    reg        phi;                         // the high byte of a pixel next
    reg        wait_;                       // one clock for the read of the video RAM / shadow / font
    reg [1:0]  rw;
    assign started = (st >= S_CLR);

    wire [3:0] fgc = cw[19:16];
    wire [3:0] bgc = cw[23:20];
    wire       inv = cw[24];
    wire [11:0] code = cw[11:0];
    wire [3:0]  typ  = cw[15:12];

    wire [8:0]  x0 = {col, 3'b000};
    wire [8:0]  x1 = x0 + 9'd7;
    wire [7:0]  y0 = {row, 4'b0000};
    wire [7:0]  y1 = y0 + 8'd15;

    reg [7:0] hdr;
    always @(*) begin
        case (hi)
        4'd0: hdr = 8'h2a;
        4'd1: hdr = {7'd0, x0[8]};
        4'd2: hdr = x0[7:0];
        4'd3: hdr = {7'd0, x1[8]};
        4'd4: hdr = x1[7:0];
        4'd5: hdr = 8'h2b;
        4'd6: hdr = 8'h00;
        4'd7: hdr = y0;
        4'd8: hdr = 8'h00;
        4'd9: hdr = y1;
        default: hdr = 8'h2c;
        endcase
    end
    wire hdr_dc = !(hi == 4'd0 || hi == 4'd5 || hi == 4'd10);

    wire pixel_on = gbits[3'd7 - pb[2:0]] ^ inv;
    wire [15:0] pcol = palette(pixel_on ? fgc : bgc);

`ifdef SPIDBG
    reg [4:0] st_d;
    always @(posedge clk) begin st_d <= st; if (st != st_d) $display("%0t st %0d -> %0d idx=%0d cw=%h sq=%h", $time, st_d, st, idx, cw, sq); end
`endif
    always @(posedge clk) begin
        put <= 1'b0;
        sw  <= 1'b0;
        if (rst) begin
            st <= S_RST0; lcd_cs <= 1'b1; lcd_rst_n <= 1'b0; tcnt <= 0; ii <= 0; x_req <= 1'b0; idx <= 0; col <= 0; row <= 0;
            cells_painted <= 16'd0; va <= 0; fa <= 0;
        end else begin
            case (st)
            // ---- the panel is reset
            S_RST0: begin
                lcd_rst_n <= 1'b0;
                if (tcnt == T_RESET) begin lcd_rst_n <= 1'b1; tcnt <= 0; st <= S_RST1; end
                else tcnt <= tcnt + 1'b1;
            end
            S_RST1: begin
                if (tcnt == D_RESET * T_UNIT) begin tcnt <= 0; ii <= 0; st <= S_INIT; end
                else tcnt <= tcnt + 1'b1;
            end
            // ---- the start sequence
            S_INIT: begin
                step <= ir;
                if (ir[9:8] == 2'd3) begin sa <= 12'd0; st <= S_CLR; end
                else if (ir[9:8] == 2'd2) begin tcnt <= 0; st <= S_IDLY; end
                else begin lcd_cs <= 1'b0; st <= S_ISEND; end
            end
            S_ISEND: if (ready) begin                                   // CS has been low for a clock
                put <= 1'b1; put_dc <= step[8]; put_byte <= step[7:0]; ii <= ii + 1'b1; st <= S_INIT;
            end
            S_IDLY: begin
                lcd_cs <= idle ? 1'b1 : lcd_cs;
                if (idle) begin
                    if (tcnt == {step[7:0], 15'd0}) begin ii <= ii + 1'b1; st <= S_INIT; end
                    else tcnt <= tcnt + 1'b1;
                end
            end
            // ---- the shadow copy is made invalid (all ones: never a real word, the cursor flag and the attribute cannot be all ones with type 15)
            S_CLR: begin
                sd <= 25'h1ffffff; sw <= 1'b1;
                if (sw) begin
                    if (sa == CELLS - 1) begin sw <= 1'b0; idx <= 0; col <= 0; row <= 0; st <= S_SCAN0; end
                    else sa <= sa + 1'b1;
                end
            end
            // ---- the walk over the cells
            S_SCAN0: begin va <= idx; sa <= idx; st <= S_SCAN1; end
            S_SCAN1: st <= S_SCAN2;                                      // the RAMs answer
            S_SCAN2: begin
                cw <= {(blink && col == cur_col && row == cur_row), vq};
                st <= S_CMP;
            end
            S_CMP: begin
                if (cw == sq) begin                                     // nothing new here: the next cell
                    if (idx == CELLS - 1) begin idx <= 0; col <= 0; row <= 0; end
                    else begin
                        idx <= idx + 1'b1;
                        if (col == COLS - 1) begin col <= 0; row <= row + 1'b1; end else col <= col + 1'b1;
                    end
                    st <= S_SCAN0;
                end else begin lcd_cs <= 1'b0; hi <= 0; st <= S_CS; end
            end
            // ---- one cell
            S_CS: st <= S_HDR;                                          // CS is low for two clocks before SCK moves
            S_HDR: if (ready) begin
                put <= 1'b1; put_dc <= hdr_dc; put_byte <= hdr;
                if (hi == 4'd10) begin line <= 0; st <= S_LINE; end
                else hi <= hi + 1'b1;
            end
            S_LINE: begin
                if (line == 5'd16) st <= S_END;
                else begin
                    fa <= {code[7:0], line[3:0]};
                    if (typ == 4'd0) begin st <= S_GET; rw <= 2'd2; end         // the font ROM answers in a clock
                    else begin
                        x_addr <= `KJ_PSRAM + `KJ_GLYPH + {5'd0, code, 5'd0} + {17'd0, line[3:0], 1'b0};
                        x_req <= 1'b1; st <= S_GETW;
                    end
                end
            end
            S_GET: begin
                if (rw != 0) rw <= rw - 1'b1;
                else begin gbits <= fq; pb <= 0; phi <= 1'b1; st <= S_PIX; end
            end
            S_GETW: if (x_ack) begin
                x_req <= 1'b0;
                gbits <= (typ == 4'd2) ? x_rdata[15:8] : x_rdata[7:0];
                pb <= 0; phi <= 1'b1; st <= S_PIX;
            end
            S_PIX: if (ready) begin
                put <= 1'b1; put_dc <= 1'b1; put_byte <= phi ? pcol[15:8] : pcol[7:0];
                if (phi) phi <= 1'b0;
                else begin
                    phi <= 1'b1;
                    if (pb == 4'd7) begin line <= line + 1'b1; st <= S_LINE; end
                    else pb <= pb + 1'b1;
                end
            end
            S_END: if (idle) begin
                lcd_cs <= 1'b1; sa <= idx; sd <= cw; sw <= 1'b1; cells_painted <= cells_painted + 1'b1; st <= S_SHADOW;
            end
            S_SHADOW: begin
                if (idx == CELLS - 1) begin idx <= 0; col <= 0; row <= 0; end
                else begin
                    idx <= idx + 1'b1;
                    if (col == COLS - 1) begin col <= 0; row <= row + 1'b1; end else col <= col + 1'b1;
                end
                st <= S_SCAN0;
            end
            default: st <= S_RST0;
            endcase
        end
    end
endmodule
