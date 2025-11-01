#!/usr/bin/env python3
import os
import argparse

from migen import *  # noqa: F401
from litex.soc.integration.soc_core import SoCCore
from litex.soc.integration.builder import Builder
from litex.soc.integration.common import SoCRegion


def import_target(target: str):
    if target == "sim":
        import importlib
        return importlib.import_module("litex_boards.targets.sim")
    elif target == "arty":
        import importlib
        return importlib.import_module("litex_boards.targets.digilent_arty")
    else:
        raise SystemExit(f"Unsupported target: {target}")


def build_soc(args):
    target = import_target(args.target)
    BaseSoC = getattr(target, "BaseSoC")

    # Build a minimal SoC with integrated RAM for sim, or board defaults
    soc = BaseSoC(
        integrated_main_ram_size=0x8000 if args.target == "sim" else None,
        sys_clk_freq=args.clk_freq,
    )

    # Add accelerator RTL source
    rtl_path = os.path.join(os.path.dirname(__file__), "..", "hw", "rtl", "vector_mac_accel.v")
    rtl_path = os.path.abspath(rtl_path)
    soc.platform.add_source(rtl_path)

    # Add Wishbone wrapper and memory-map it into system bus
    from hw.wb_accel import VectorMACWishbone

    accel = VectorMACWishbone(data_width=32, addr_width=16)
    soc.submodules.accel = accel

    origin = int(args.accel_origin, 0)
    size = int(args.accel_size, 0)
    soc.bus.add_slave(name="accel", slave=accel.bus, region=SoCRegion(origin=origin, size=size, mode="rw"))

    # Optional: expose region to software
    soc.add_memory_region("accel", origin=origin, length=size)

    builder = Builder(soc, output_dir=os.path.abspath("soc_build"), csr_csv="csr.csv")
    builder.build(run=False)
    print("[build] SoC generated (gateware and software stubs). Use the board's flow to run.")


def main():
    parser = argparse.ArgumentParser(description="Build LiteX SoC with Vector MAC accelerator")
    parser.add_argument("--target", default="sim", choices=["sim", "arty"], help="Target platform")
    parser.add_argument("--clk-freq", dest="clk_freq", default=50e6, type=float, help="System clock frequency")
    parser.add_argument("--accel-origin", default="0x30000000", help="Accelerator base address")
    parser.add_argument("--accel-size", default="0x10000", help="Accelerator region size")
    args = parser.parse_args()

    build_soc(args)


if __name__ == "__main__":
    main()


