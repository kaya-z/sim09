// SPDX-License-Identifier: MIT
// The pixel clock of the SPI screen: the divided output of the system PLL (67.5 MHz / 2 = 33.75 MHz), as for the LCD screen (lcd/constraints/lcd.sdc).
create_generated_clock -name clk_pix -source [get_pins {pll/rpll_inst/CLKOUT}] -master_clock clk81 -divide_by 2 [get_pins {pll/rpll_inst/CLKOUTD}]

// The system logic and the screen share only the console bytes (a toggle, sampled three times) and the PSRAM look-ups (psram_xfer: request and acknowledge toggles).
set_false_path -from [get_clocks {clk81}] -to [get_clocks {clk_pix}]
set_false_path -from [get_clocks {clk_pix}] -to [get_clocks {clk81}]
