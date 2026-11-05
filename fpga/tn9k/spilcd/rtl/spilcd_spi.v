// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The SPI master of the ILI9341 screen (docs/LCD_SPI.md): mode 0 (SCK idles low, the data is taken on the rising edge), most significant bit first, with a data / command
// line (D/CX) that goes with every byte.  One bit takes two clocks: SCK is low for the first (the data and D/CX change with the falling edge) and high for the second,
// so SCK = clk / (2 * HALF) (clk = the pixel clock 33.75 MHz: 16.875 MHz with HALF = 1; the datasheet allows 10 MHz: HALF = 2 gives 8.4375 MHz, see docs/LCD_SPI.md).
//
// A byte can be put in while the one before is still being sent (ready = the holding place is free), so that the bytes follow each other without a gap.

module spilcd_spi #(
    parameter HALF = 1                      // clocks per half period of SCK: 1 = 16.875 MHz, 2 = 8.4375 MHz (inside the 10 MHz of the datasheet)
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       put,                  // a byte (with dc) for the holding place; only when ready
    input  wire       put_dc,               // 0 = command, 1 = data
    input  wire [7:0] put_byte,
    output wire       ready,
    output wire       idle,                 // nothing is being sent and nothing waits
    output reg        sck,
    output reg        mosi,
    output reg        dc
);
    reg       nv;                           // the holding place is full
    reg       ndc;
    reg [7:0] nb;
    reg       act;                          // a byte is on its way
    reg       ph;                           // 0: SCK low, 1: SCK high
    reg [2:0] bit_;
    reg [3:0] hc;                           // the clocks of the half period
    wire      tick = (hc == HALF - 1);
    reg [7:0] sh;

    assign ready = !nv && !put;              // (a byte that is being put this clock is not in the holding place yet)
    assign idle  = !act && !nv;

    always @(posedge clk) begin
        if (rst) begin
            nv <= 1'b0; act <= 1'b0; ph <= 1'b0; sck <= 1'b0; mosi <= 1'b0; dc <= 1'b0; bit_ <= 3'd0; hc <= 4'd0;
        end else begin
            if (put && !nv) begin nv <= 1'b1; ndc <= put_dc; nb <= put_byte; end
            if (!act) begin
                sck <= 1'b0;
                if (nv) begin                               // start a byte: the first bit and D/CX go out, SCK stays low for this clock
                    act <= 1'b1; ph <= 1'b0; hc <= 4'd0; bit_ <= 3'd0; sh <= {nb[6:0], 1'b0}; mosi <= nb[7]; dc <= ndc;
                    nv <= 1'b0;
                end
            end else if (!tick) begin
                hc <= hc + 1'b1;
            end else if (!ph) begin
                sck <= 1'b1; ph <= 1'b1; hc <= 4'd0;
            end else begin
                sck <= 1'b0; ph <= 1'b0; hc <= 4'd0;
                if (bit_ == 3'd7) begin                     // the byte is out
                    if (nv) begin                            // the next one without a gap
                        bit_ <= 3'd0; sh <= {nb[6:0], 1'b0}; mosi <= nb[7]; dc <= ndc; nv <= 1'b0;
                    end else act <= 1'b0;
                end else begin
                    bit_ <= bit_ + 1'b1; sh <= {sh[6:0], 1'b0}; mosi <= sh[7];
                end
            end
        end
    end
endmodule
