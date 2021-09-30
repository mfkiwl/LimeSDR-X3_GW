##-----------------------------------------------------------------------------
##
## (c) Copyright 2010-2011 Xilinx, Inc. All rights reserved.
##
## This file contains confidential and proprietary information
## of Xilinx, Inc. and is protected under U.S. and
## international copyright and other intellectual property
## laws.
##
## DISCLAIMER
## This disclaimer is not a license and does not grant any
## rights to the materials distributed herewith. Except as
## otherwise provided in a valid license issued to you by
## Xilinx, and to the maximum extent permitted by applicable
## law: (1) THESE MATERIALS ARE MADE AVAILABLE "AS IS" AND
## WITH ALL FAULTS, AND XILINX HEREBY DISCLAIMS ALL WARRANTIES
## AND CONDITIONS, EXPRESS, IMPLIED, OR STATUTORY, INCLUDING
## BUT NOT LIMITED TO WARRANTIES OF MERCHANTABILITY, NON-
## INFRINGEMENT, OR FITNESS FOR ANY PARTICULAR PURPOSE; and
## (2) Xilinx shall not be liable (whether in contract or tort,
## including negligence, or under any other theory of
## liability) for any loss or damage of any kind or nature
## related to, arising under or in connection with these
## materials, including for any direct, or any indirect,
## special, incidental, or consequential loss or damage
## (including loss of data, profits, goodwill, or any type of
## loss or damage suffered as a result of any action brought
## by a third party) even if such damage or loss was
## reasonably foreseeable or Xilinx had been advised of the
## possibility of the same.
##
## CRITICAL APPLICATIONS
## Xilinx products are not designed or intended to be fail-
## safe, or for use in any application requiring fail-safe
## performance, such as life-support or safety devices or
## systems, Class III medical devices, nuclear facilities,
## applications related to the deployment of airbags, or any
## other applications that could lead to death, personal
## injury, or severe property or environmental damage
## (individually and collectively, "Critical
## Applications"). Customer assumes the sole risk and
## liability of any use of Xilinx products in Critical
## Applications, subject only to applicable laws and
## regulations governing limitations on product liability.
##
## THIS COPYRIGHT NOTICE AND DISCLAIMER MUST BE RETAINED AS
## PART OF THIS FILE AT ALL TIMES.
##
##-----------------------------------------------------------------------------
## Project    : Series-7 Integrated Block for PCI Express
## File       : pcie_x4-PCIE_X0Y0.xdc
## Version    : 3.3
#
###############################################################################
# Vivado - PCIe GUI / User Configuration
###############################################################################
#
# Family       - artix7
# Part         - xc7a200t
# Package      - fbg484
# Speed grade  - -2
# PCIe Block   - PCIE_X0Y0
#
# Link Speed   - 2
# Link Width   - X4
# AXIST Width  - 64-bit
# AXIST Frequ  - 3
#
###############################################################################
# User Time Names / User Time Groups / Time Specs
###############################################################################

###############################################################################
# User Physical Constraints
###############################################################################
create_clock -period 10.000 -name PCIE_REFCLK -waveform {0.000 5.000} [get_ports PCIE_REFCLK_P]

###############################################################################
# Pinout and Related I/O Constraints
###############################################################################


###############################################################################
# Physical Constraints
###############################################################################
#
# Transceiver instance placement.  This constraint selects the
# transceivers to be used, which also dictates the pinout for the
# transmit and receive differential pairs.  Please refer to the
# Virtex-7 GT Transceiver User Guide (UG) for more information.
#

# PCIe Lane 0
set_property LOC GTPE2_CHANNEL_X0Y7 [get_cells {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
# PCIe Lane 1
set_property LOC GTPE2_CHANNEL_X0Y6 [get_cells {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[1].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
# PCIe Lane 2
set_property LOC GTPE2_CHANNEL_X0Y5 [get_cells {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[2].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]
# PCIe Lane 3
set_property LOC GTPE2_CHANNEL_X0Y4 [get_cells {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[3].gt_wrapper_i/gtp_channel.gtpe2_channel_i}]

# GTP Common Placement
set_property LOC GTPE2_COMMON_X0Y1 [get_cells {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].pipe_quad.gt_common_enabled.gt_common_int.gt_common_i/qpll_wrapper_i/gtp_common.gtpe2_common_i}]

# PCI Express Block placement. This constraint selects the PCI Express
# Block to be used.
#
set_property LOC PCIE_X0Y0 [get_cells inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/pcie_top_i/pcie_7x_i/pcie_block_i]

#
# BlockRAM placement
#


###############################################################################
# Timing Constraints
###############################################################################
#
create_clock -period 10.000 -name txoutclk_x0y0 [get_pins {inst2_pcie_top/inst1_litepcie_top/inst0_litepcie_core/pcie_phy/pcie_support_i/pcie_i/inst/inst/gt_top_i/pipe_wrapper_i/pipe_lane[0].gt_wrapper_i/gtp_channel.gtpe2_channel_i/TXOUTCLK}]
#
#
set_false_path -through [get_pins -filter REF_PIN_NAME=~PLPHYLNKUPN -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ * }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~PLRECEIVEDHOTRST -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ * }]]

#------------------------------------------------------------------------------
# Asynchronous Paths
#------------------------------------------------------------------------------
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXELECIDLE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~TXPHINITDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~TXPHALIGNDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~TXDLYSRESETDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXDLYSRESETDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXPHALIGNDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXCDRLOCK -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~CFGMSGRECEIVEDPMETO -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ * }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~PLL0LOCK -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXPMARESETDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~RXSYNCDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]
set_false_path -through [get_pins -filter REF_PIN_NAME=~TXSYNCDONE -of_objects [get_cells -hierarchical -filter { PRIMITIVE_TYPE =~ IO.gt.* }]]

###############################################################################
# End
###############################################################################




















