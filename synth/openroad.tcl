# Minimal OpenROAD PnR flow for vector_mac_accel
# Requires environment variables:
#   TECH_LEF : path to technology LEF
#   LIB_LEF  : path to standard cell LEF
#   LIB_LIB  : path to liberty timing file

set ::env(RESULTS_DIR) [file normalize ./results]
set ::env(REPORTS_DIR) [file normalize ./reports]

file mkdir $::env(RESULTS_DIR)
file mkdir $::env(REPORTS_DIR)

set tech_lef $::env(TECH_LEF)
set lib_lef  $::env(LIB_LEF)
set lib_lib  $::env(LIB_LIB)

read_lef $tech_lef
read_lef $lib_lef
read_liberty $lib_lib

read_verilog results/netlist.v
link_design vector_mac_accel

# Simple floorplan
initialize_floorplan -die_area {0 0 200 200} -core_area {10 10 190 190}
place_pins -random -hor_layers {metal2} -ver_layers {metal3}

# Place/CTS/Route (very minimal)
global_placement
tapcell_or
pdn_gen -verbose false
global_placement
repair_design

clock_tree_synthesis -root_buf CLKBUF_X1 -buf_list {CLKBUF_X1}
global_routing
detailed_routing

report_design_area > $::env(REPORTS_DIR)/area.rpt
report_worst_slack > $::env(REPORTS_DIR)/timing.rpt

write_def $::env(RESULTS_DIR)/design.def
write_db  $::env(RESULTS_DIR)/design.odb


