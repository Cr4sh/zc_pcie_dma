
###############################################################################
# Timing Constraints
###############################################################################

create_clock -name sys_clk -period 10 [get_ports sys_clk_p]
 
set_false_path -to [get_pins {pcie_7x_0_support_i/pipe_clock_i/pclk_i1_bufgctrl.pclk_i1/S0}]
set_false_path -to [get_pins {pcie_7x_0_support_i/pipe_clock_i/pclk_i1_bufgctrl.pclk_i1/S1}]

set_case_analysis 1 [get_pins {pcie_7x_0_support_i/pipe_clock_i/pclk_i1_bufgctrl.pclk_i1/S0}]
set_case_analysis 0 [get_pins {pcie_7x_0_support_i/pipe_clock_i/pclk_i1_bufgctrl.pclk_i1/S1}]
set_property DONT_TOUCH true [get_cells -of [get_nets -of [get_pins {pcie_7x_0_support_i/pipe_clock_i/pclk_i1_bufgctrl.pclk_i1/S0}]]]


###############################################################################
# Pinout and Related I/O Constraints
###############################################################################

# reset
set_property IOSTANDARD LVCMOS15 [get_ports sys_rst_n]
set_property PULLUP true [get_ports sys_rst_n]
set_property LOC AK23 [get_ports sys_rst_n]

# user LED #0 
set_property IOSTANDARD LVCMOS15 [get_ports led_0]
set_property LOC W21 [get_ports led_0]

# user LED #2
set_property IOSTANDARD LVCMOS15 [get_ports led_2]
set_property LOC G2 [get_ports led_2]

# user LED #3
set_property IOSTANDARD LVCMOS15 [get_ports led_3]
set_property LOC Y21 [get_ports led_3]

set_false_path -to [get_ports -filter {NAME=~led_*}]


###############################################################################
# Physical Constraints
###############################################################################

set_property LOC IBUFDS_GTE2_X0Y6 [get_cells refclk_ibuf]

set_false_path -from [get_ports sys_rst_n]

