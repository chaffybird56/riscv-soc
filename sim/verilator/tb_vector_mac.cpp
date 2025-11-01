#include <verilated.h>
#include <verilated_vcd_c.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include "Vvector_mac_accel.h"

static vluint64_t sim_time = 0;
static const uint64_t MAX_CYC = 1000000ull;

static void tick(Vvector_mac_accel *top, VerilatedVcdC *tfp) {
    top->clk = 0; top->eval(); if (tfp) { tfp->dump(sim_time++); }
    top->clk = 1; top->eval(); if (tfp) { tfp->dump(sim_time++); }
}

static void reset(Vvector_mac_accel *top, VerilatedVcdC *tfp) {
    top->rst = 1;
    top->wb_cyc = 0; top->wb_stb = 0; top->wb_we = 0; top->wb_sel = 0xF;
    tick(top, tfp); tick(top, tfp); tick(top, tfp);
    top->rst = 0;
}

static uint32_t wb_read(Vvector_mac_accel *top, VerilatedVcdC *tfp, uint32_t adr) {
    top->wb_adr = adr;
    top->wb_we  = 0;
    top->wb_dat_w = 0;
    top->wb_sel = 0xF;
    top->wb_cyc = 1; top->wb_stb = 1;
    do { tick(top, tfp); } while (!top->wb_ack);
    uint32_t r = top->wb_dat_r;
    top->wb_cyc = 0; top->wb_stb = 0;
    tick(top, tfp);
    return r;
}

static void wb_write(Vvector_mac_accel *top, VerilatedVcdC *tfp, uint32_t adr, uint32_t data) {
    top->wb_adr = adr;
    top->wb_we  = 1;
    top->wb_dat_w = data;
    top->wb_sel = 0xF;
    top->wb_cyc = 1; top->wb_stb = 1;
    do { tick(top, tfp); } while (!top->wb_ack);
    top->wb_cyc = 0; top->wb_stb = 0;
    top->wb_we  = 0;
    tick(top, tfp);
}

int main(int argc, char **argv) {
    Verilated::commandArgs(argc, argv);
    Verilated::traceEverOn(true);

    Vvector_mac_accel *top = new Vvector_mac_accel;
    VerilatedVcdC *tfp = new VerilatedVcdC;
    top->trace(tfp, 99);
    tfp->open("wave.vcd");

    reset(top, tfp);

    // Constants matching RTL map (word addresses within peripheral)
    const uint32_t REG_CTRL  = 0x0000;
    const uint32_t REG_STAT  = 0x0001;
    const uint32_t REG_LEN   = 0x0002;
    const uint32_t REG_KLEN  = 0x0003;
    const uint32_t REG_CLO   = 0x0004;
    const uint32_t REG_CHI   = 0x0005;
    const uint32_t REG_SHFT  = 0x0006;
    const uint32_t A_BASE    = 0x1000;
    const uint32_t B_BASE    = 0x2000;
    const uint32_t O_BASE    = 0x3000;

    // DOT benchmark: n=8, shift=15
    const uint32_t N = 8;
    for (uint32_t i = 0; i < N; i++) {
        uint32_t a = (i * 13u) & 0xFFFFu;
        uint32_t b = ((N - i) * 7u) & 0xFFFFu;
        wb_write(top, tfp, A_BASE + i, a);
        wb_write(top, tfp, B_BASE + i, b);
    }

    wb_write(top, tfp, REG_LEN,  N);
    wb_write(top, tfp, REG_SHFT, 15);

    // Start (mode=dot: bit2=0)
    wb_write(top, tfp, REG_CTRL, 1u << 0);

    // Wait done
    uint64_t w = 0;
    while (((wb_read(top, tfp, REG_STAT) >> 1) & 1u) == 0u) {
        if (++w > MAX_CYC) {
            printf("Timeout waiting for DOT done\n");
            break;
        }
    }
    uint32_t out0 = wb_read(top, tfp, O_BASE + 0);
    uint32_t clo  = wb_read(top, tfp, REG_CLO);
    uint32_t chi  = wb_read(top, tfp, REG_CHI);
    uint64_t cyc  = ((uint64_t)chi << 32) | clo;
    printf("DOT: out0=%d, cycles=%llu\n", (int32_t)out0, (unsigned long long)cyc);

    // Clear
    wb_write(top, tfp, REG_CTRL, 1u << 1);

    // CONV benchmark: out_len=4, klen=3, shift=15
    for (uint32_t i = 0; i < 4 + 3; i++) {
        uint32_t a = (i * 13u) & 0xFFFFu;
        wb_write(top, tfp, A_BASE + i, a);
    }
    for (uint32_t j = 0; j < 3; j++) {
        uint32_t b = ((3 - j) * 7u) & 0xFFFFu;
        wb_write(top, tfp, B_BASE + j, b);
    }
    wb_write(top, tfp, REG_LEN,  4);
    wb_write(top, tfp, REG_KLEN, 3);
    wb_write(top, tfp, REG_SHFT, 15);
    // Start conv (bit2=1)
    wb_write(top, tfp, REG_CTRL, (1u << 2) | (1u << 0));

    w = 0;
    while (((wb_read(top, tfp, REG_STAT) >> 1) & 1u) == 0u) {
        if (++w > MAX_CYC) {
            printf("Timeout waiting for CONV done\n");
            break;
        }
    }
    uint32_t y0 = wb_read(top, tfp, O_BASE + 0);
    uint32_t y1 = wb_read(top, tfp, O_BASE + 1);
    printf("CONV: y0=%d, y1=%d\n", (int32_t)y0, (int32_t)y1);

    tfp->close();
    delete tfp;
    delete top;
    return 0;
}


