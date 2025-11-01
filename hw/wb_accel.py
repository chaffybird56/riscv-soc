from migen import *
from litex.soc.interconnect import wishbone
from litex.soc.interconnect.csr import *  # noqa: F401 (reserved for future CSR exposure)
from litex.soc.integration.soc import SoCRegion  # noqa: F401
from litex.build.generic_platform import *  # noqa: F401


class VectorMACWishbone(Module):
    def __init__(self, data_width: int = 32, addr_width: int = 16):
        self.bus = wishbone.Interface(data_width=data_width, adr_width=addr_width)

        # Bridge Wishbone to blackbox Verilog
        self.specials += Instance(
            "vector_mac_accel",
            p_ADDR_WIDTH=addr_width,
            p_DATA_WIDTH=data_width,
            i_clk=ClockSignal(),
            i_rst=ResetSignal(),
            i_wb_cyc=self.bus.cyc,
            i_wb_stb=self.bus.stb,
            i_wb_we=self.bus.we,
            i_wb_sel=self.bus.sel,
            i_wb_adr=self.bus.adr,
            i_wb_dat_w=self.bus.dat_w,
            o_wb_dat_r=self.bus.dat_r,
            o_wb_ack=self.bus.ack,
        )


