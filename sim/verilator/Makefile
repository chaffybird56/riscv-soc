VERILATOR ?= verilator
TOP        = vector_mac_accel
RTL        = ../../hw/rtl/vector_mac_accel.v
TB         = tb_vector_mac.cpp

all: build run

build:
	$(VERILATOR) -cc $(RTL) --trace --exe $(TB) -O2 -CFLAGS -std=c++17
	$(MAKE) -C obj_dir -f V$(TOP).mk V$(TOP)

run:
	./obj_dir/V$(TOP)

clean:
	rm -rf obj_dir *.vcd

.PHONY: all build run clean


