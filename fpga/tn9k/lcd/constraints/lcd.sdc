// SPDX-License-Identifier: MIT
// The LCD pixel clock: the divided output of the system PLL (67.5 MHz / 2 = 33.75 MHz), from the same VCO as the system clock.
create_generated_clock -name clk_pix -source [get_pins {pll/rpll_inst/CLKOUT}] -master_clock clk81 -divide_by 2 [get_pins {pll/rpll_inst/CLKOUTD}]

// As for the HDMI screen: the system logic and the display share only the video RAM, whose two ports have their own clocks, and
// the cursor position, which changes rarely and is sampled twice; the console bytes cross with a toggle.
set_false_path -from [get_clocks {clk81}] -to [get_clocks {clk_pix}]
set_false_path -from [get_clocks {clk_pix}] -to [get_clocks {clk81}]
