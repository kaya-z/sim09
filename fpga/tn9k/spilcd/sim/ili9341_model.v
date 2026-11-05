// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// A behavioural model of the ILI9341 on a 4-wire SPI interface, for the test bench of the SPI screen (docs/LCD_SPI.md).  It keeps the frame memory
// (landscape: 320 x 240 RGB565), follows the commands that the screen uses (01h 11h 29h 2Ah 2Bh 2Ch 36h 3Ah) and counts what the datasheet forbids:
//
//   fast        SCL cycles shorter than the 100 ns of the datasheet (tscycw).  The screen runs at 16.875 MHz on purpose: the number is information, not an error.
//   bad_setup   CSX to the first SCL under 40 ns (tcss), D/CX changing less than 10 ns before SCL (tas)
//   bad_early   a command inside the 5 ms after a hardware / software reset or after sleep out
//   bad_format  pixel data with a pixel format other than 16 bits (3Ah = 55h)
//   bad_state   pixel data before the display is on, or in a mode other than landscape (MV = 0 is not modelled; MX / MY mirror the axes)
//
// Everything is sampled on 'clk' (the pixel clock of the screen; SCK = clk / 2 is synchronous to it), CLK_NS gives the time of a clock.
module ili9341_model #(
    parameter real CLK_NS = 29.63          // the period of 'clk' (33.75 MHz)
) (
    input  wire        clk,
    input  wire        rst_n,
    input  wire        cs_n,
    input  wire        sck,
    input  wire        mosi,
    input  wire        dc,
    // the frame memory, read by the test bench
    input  wire [16:0] gram_a,
    output wire [15:0] gram_q,
    // what happened
    output reg  [31:0] n_cmd,
    output reg  [31:0] n_pix,
    output reg         disp_on,
    output reg         sleeping,
    output reg  [31:0] fast,
    output reg  [31:0] bad_setup,
    output reg  [31:0] bad_early,
    output reg  [31:0] bad_format,
    output reg  [31:0] bad_state,
    output reg  [7:0]  madctl,
    output reg  [7:0]  colmod,
    output reg  [55:0] seq               // the first 7 commands, 8 bits each (the oldest in the low byte)
);
    reg [15:0] gram [0:76799];
    assign gram_q = gram[gram_a];

    reg [39:0] now = 40'd0;
    reg        sck_d = 1'b0, cs_d = 1'b1, dc_d = 1'b0, rst_d = 1'b1;
    reg [39:0] t_event, t_sck_r, t_cs_f, t_dc_ch;
    reg [3:0]  nbits;
    reg [7:0]  sh, cmd;
    reg [2:0]  pi;
    reg [8:0]  xs, xe, x;
    reg [7:0]  ys, ye, y;
    reg [7:0]  hi;
    reg        have_hi;
    reg [2:0]  nseq;
    reg [8:0]  px;
    reg [7:0]  py;
    reg        byte_done;

    integer k;
    initial begin
        for (k = 0; k < 76800; k = k + 1) gram[k] = 16'h0000;
        n_cmd = 0; n_pix = 0; disp_on = 0; sleeping = 1; fast = 0; bad_setup = 0; bad_early = 0; bad_format = 0; bad_state = 0; madctl = 0; colmod = 8'h66; seq = 0;
        t_event = 0; t_sck_r = 0; t_cs_f = 0; t_dc_ch = 0; nbits = 0; sh = 0; cmd = 0; pi = 0; xs = 0; xe = 319; x = 0; ys = 0; ye = 239; y = 0; nseq = 0; have_hi = 0; hi = 0;
    end

    function real ns; input [39:0] c; ns = c * CLK_NS; endfunction

    always @(posedge clk) begin
        now <= now + 1'b1;
        sck_d <= sck; cs_d <= cs_n; dc_d <= dc; rst_d <= rst_n;
        if (dc != dc_d) t_dc_ch <= now;
        if (!cs_n && cs_d) begin t_cs_f <= now; nbits <= 0; end

        if (rst_d && !rst_n) begin                                  // hardware reset: sleep in, the defaults
            sleeping <= 1'b1; disp_on <= 1'b0; madctl <= 8'h00; colmod <= 8'h66; nbits <= 0;
        end
        if (!rst_d && rst_n) t_event <= now;                        // 5 ms until the next command

        if (sck && !sck_d && !cs_n && rst_n) begin                  // a rising edge of SCL
            if (t_sck_r != 0 && ns(now - t_sck_r) < 99.5) fast <= fast + 1;
            t_sck_r <= now;
            if (nbits == 0 && ns(now - t_cs_f) < 39.5) bad_setup <= bad_setup + 1;
            if (ns(now - t_dc_ch) < 9.5) bad_setup <= bad_setup + 1;
            sh = {sh[6:0], mosi};
            byte_done = (nbits == 4'd7);
            nbits <= byte_done ? 4'd0 : nbits + 1'b1;
            if (byte_done) begin
                if (!dc) begin                                      // a command
                    n_cmd <= n_cmd + 1;
                    cmd = sh; pi = 0; have_hi = 0;
                    if (nseq < 3'd7) begin seq[nseq*8 +: 8] <= sh; nseq <= nseq + 1'b1; end
                    if (t_event != 0 && ns(now - t_event) < 4900000.0) bad_early <= bad_early + 1;
                    case (sh)
                    8'h01: begin sleeping <= 1'b1; disp_on <= 1'b0; madctl <= 8'h00; t_event <= now; end
                    8'h11: begin sleeping <= 1'b0; t_event <= now; end
                    8'h29: begin disp_on <= 1'b1; t_event <= now; end
                    8'h2c: begin x = xs; y = ys; if (!disp_on) bad_state <= bad_state + 1; end
                    default: ;
                    endcase
                end else begin                                      // a parameter or pixel data
                    case (cmd)
                    8'h2a: begin
                        case (pi) 0: xs[8] = sh[0]; 1: xs[7:0] = sh; 2: xe[8] = sh[0]; 3: xe[7:0] = sh; default: ; endcase
                        if (pi == 3) x = xs;
                        pi = pi + 1;
                    end
                    8'h2b: begin
                        case (pi) 1: ys = sh; 3: ye = sh; default: ; endcase
                        if (pi == 3) y = ys;
                        pi = pi + 1;
                    end
                    8'h36: madctl <= sh;
                    8'h3a: colmod <= sh;
                    8'h2c: begin
                        if (colmod != 8'h55) bad_format <= bad_format + 1;
                        if (!madctl[5]) bad_state <= bad_state + 1;
                        if (!have_hi) begin hi = sh; have_hi = 1; end
                        else begin
                            px = madctl[6] ? (9'd319 - x) : x;
                            py = madctl[7] ? (8'd239 - y) : y;
                            if (px < 320 && py < 240) gram[{9'd0, py} * 320 + {8'd0, px}] = {hi, sh};
                            n_pix <= n_pix + 1;
                            if (x == xe) begin x = xs; y = (y == ye) ? ys : y + 1; end else x = x + 1;
                            have_hi = 0;
                        end
                    end
                    default: ;
                    endcase
                end
            end
        end
    end
endmodule
