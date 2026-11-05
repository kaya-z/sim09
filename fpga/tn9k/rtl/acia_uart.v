// SPDX-License-Identifier: MIT
// Copyright (c) 2026 sim09 contributors
//
// The console port of the sim09 / sbc09 "v09" machine: a 6850 style ACIA (control / status at +0,
// data at +1) on a 8N1 UART.
//
//   status:  bit0 RDRF (a received character is waiting), bit1 TDRE (the transmitter can take one),
//            bit7 IRQ (the ACIA requests an interrupt)
//   control: bit7 RIE (interrupt for RDRF), bits6-5 = 01 TIE (interrupt for TDRE), bits1-0 = 11 master
//            reset (no interrupts until the next write); the other bits (clock divider, word format)
//            are ignored, the line is always 8N1 at BAUD_DIV
//   data:    read = the oldest received character (the last one again when none is waiting, like the
//            simulator), write = send
//
// Guests that poll (FUZIX, MK09, the monitor) never write the control register: no interrupts.
// OS-9 enables RIE and TIE and has the interrupt request on the IRQ line (irq output).
//
// The simulator's console is not paced, the FPGA's is: a 256 character receive FIFO keeps what
// is typed or pasted between two polls of a guest that looks at the port 50 times a second (an
// interrupt driven guest needs far less).  The transmitter has a FIFO of 15 characters: TDRE means
// that the FIFO has room, so that the transmit interrupt handler can write a burst and a handler that
// needs more than one character time to be entered still keeps the line busy.

module acia_uart #(
    parameter BAUD_DIV = 586                // clk / baud (67.5 MHz / 115200)
) (
    input  wire       clk,
    input  wire       rst,
    input  wire       rxd,
    output reg        txd,
    input  wire       rd_data,              // one clk pulse: the guest reads the data register
    input  wire       wr_data,              // one clk pulse: the guest writes the data register
    input  wire [7:0] wdata,
    input  wire       wr_ctl,               // one clk pulse: the guest writes the control register
    input  wire [7:0] cdata,
    output wire       tx_full,              // the transmitter cannot take a character now
    output wire [7:0] status,
    output wire [7:0] rdata,
    output reg        irq                   // interrupt request (level)
);

    // ---------------------------------------------------------------- receiver
    reg [1:0]  rxs;
    reg [9:0]  rbaud;
    reg [3:0]  rbit;
    reg        rbusy;
    reg [7:0]  rsh;

    reg [7:0]  fifo [0:255];
    reg [7:0]  wp, rp;
    reg [7:0]  last;
    reg        push;
    reg [7:0]  push_d;
    reg        nonempty;

    wire       empty = (wp == rp);
    reg        full;
    reg [7:0]  rq;
    always @(posedge clk) rq <= fifo[rp];                    // the head of the queue, a clock late (reads are far apart)
    always @(posedge clk) full <= ((wp + 1'b1) == rp);       // 255 characters at most (a clock late: pushes are far apart)
    wire       pop   = rd_data && !empty;

    always @(posedge clk) begin
        rxs  <= {rxs[0], rxd};
        push <= 1'b0;
        if (rst) begin
            rbusy <= 1'b0;
        end else if (!rbusy) begin
            if (!rxs[1]) begin                  // start bit
                rbusy <= 1'b1;
                rbaud <= BAUD_DIV / 2;
                rbit  <= 4'd0;
            end
        end else if (rbaud != 0) begin
            rbaud <= rbaud - 1'b1;
        end else begin
            rbaud <= BAUD_DIV - 1;
            if (rbit == 4'd0) begin             // middle of the start bit
                if (rxs[1]) rbusy <= 1'b0;      // glitch
                rbit <= 4'd1;
            end else if (rbit <= 4'd8) begin
                rsh  <= {rxs[1], rsh[7:1]};
                rbit <= rbit + 1'b1;
            end else begin                      // stop bit
                rbusy  <= 1'b0;
                push   <= 1'b1;
                push_d <= rsh;
            end
        end
    end

    always @(posedge clk) begin
        if (rst) begin
            wp <= 8'd0; rp <= 8'd0; last <= 8'h02;
        end else begin
            if (push && !full) begin
                fifo[wp] <= push_d;
                wp <= wp + 1'b1;
            end
            if (pop) begin
                last <= rq;
                rp <= rp + 1'b1;
            end
        end
    end

    // ---------------------------------------------------------------- transmitter
    reg [7:0] tfifo [0:15];
    reg [3:0] twp, trp;
    wire      tempty = (twp == trp);
    reg       tfull;                                          // 15 characters in the FIFO (a clock late: writes are far apart)
    always @(posedge clk) tfull <= ((twp + 1'b1) == trp);
    reg [7:0] tq;
    always @(posedge clk) tq <= tfifo[trp];                    // the head of the queue, a clock late
    reg [9:0] tsh;
    reg [3:0] tbit;
    reg [9:0] tbaud;
    reg       tbusy;
    reg       tload;                                           // tq is valid (a clock after the FIFO head moved)

    always @(posedge clk) begin
        tload <= 1'b0;
        if (rst) begin
            txd <= 1'b1; tbusy <= 1'b0; twp <= 4'd0; trp <= 4'd0;
        end else begin
            if (wr_data && !tfull) begin
                tfifo[twp] <= wdata;
                twp <= twp + 1'b1;
            end
            if (!tbusy) begin
                if (!tempty && !tload) begin
                    tload <= 1'b1;                              // tq follows the new head in the next clock
                end else if (tload) begin
                    tsh   <= {1'b1, tq, 1'b0};
                    trp   <= trp + 1'b1;
                    tbit  <= 4'd0;
                    tbaud <= BAUD_DIV - 1;
                    tbusy <= 1'b1;
                    txd   <= 1'b0;
                end
            end else if (tbaud != 0) begin
                tbaud <= tbaud - 1'b1;
            end else begin
                tbaud <= BAUD_DIV - 1;
                if (tbit == 4'd9) begin
                    tbusy <= 1'b0;
                    txd   <= 1'b1;
                end else begin
                    tbit <= tbit + 1'b1;
                    tsh  <= {1'b1, tsh[9:1]};
                    txd  <= tsh[1];
                end
            end
        end
    end

    // ---------------------------------------------------------------- control register and interrupt
    reg [7:0] cr;
    wire      cr_rst = (cr[1:0] == 2'b11);
    always @(posedge clk) begin
        if (rst) cr <= 8'h03;
        else if (wr_ctl) cr <= (cdata[1:0] == 2'b11) ? 8'h03 : cdata;     // master reset: interrupts off
    end
    wire      irq_c = !cr_rst && ((cr[7] && !empty) || (cr[6:5] == 2'b01 && !tfull));

    // registered: the guest reads the port at most once per bus cycle, long after a change
    reg [7:0] status_r, rdata_r;
    always @(posedge clk) begin
        irq      <= irq_c;
        status_r <= {irq_c, 5'b0, !tfull, !empty};
        rdata_r  <= !empty ? rq : last;
    end
    assign tx_full = tfull;
    assign status  = status_r;
    assign rdata   = rdata_r;

endmodule
