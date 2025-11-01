#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#ifndef ACCEL_BASE
#define ACCEL_BASE 0x30000000u
#endif

// Register word offsets
#define REG_CTRL        0x00u // bit0 start, bit1 clear, bit2 mode(0=dot,1=conv)
#define REG_STATUS      0x01u // bit0 busy, bit1 done
#define REG_LENGTH      0x02u
#define REG_KLEN        0x03u
#define REG_CYCLES_LO   0x04u
#define REG_CYCLES_HI   0x05u
#define REG_SCALE_SHIFT 0x06u

// Memory word-base offsets
#define A_BASE_W  0x1000u
#define B_BASE_W  0x2000u
#define O_BASE_W  0x3000u

static inline volatile uint32_t *reg_ptr(uint32_t word_off) {
    return (volatile uint32_t *)(ACCEL_BASE + (word_off << 2));
}

static inline volatile uint32_t *memA_ptr(uint32_t idx) {
    return (volatile uint32_t *)(ACCEL_BASE + ((A_BASE_W + idx) << 2));
}

static inline volatile uint32_t *memB_ptr(uint32_t idx) {
    return (volatile uint32_t *)(ACCEL_BASE + ((B_BASE_W + idx) << 2));
}

static inline volatile uint32_t *memO_ptr(uint32_t idx) {
    return (volatile uint32_t *)(ACCEL_BASE + ((O_BASE_W + idx) << 2));
}

static inline uint64_t rdcycle(void) {
    uint64_t v;
    asm volatile ("rdcycle %0" : "=r"(v));
    return v;
}

static void accel_clear(void) {
    *reg_ptr(REG_CTRL) = (1u << 1); // clear
}

static void accel_config(uint32_t length, uint32_t klen, uint32_t shift) {
    *reg_ptr(REG_LENGTH) = length;
    *reg_ptr(REG_KLEN) = klen;
    *reg_ptr(REG_SCALE_SHIFT) = shift;
}

static void accel_start(bool conv_mode) {
    uint32_t ctrl = 0u;
    if (conv_mode) ctrl |= (1u << 2);
    ctrl |= (1u << 0);
    *reg_ptr(REG_CTRL) = ctrl;
}

static void accel_wait_done(void) {
    while ((*reg_ptr(REG_STATUS) & (1u << 1)) == 0u) {
        // spin until done
    }
}

static void fill_inputs(uint32_t *a, uint32_t *b, uint32_t n) {
    // Deterministic data: simple ramp with wrap-around
    for (uint32_t i = 0; i < n; i++) {
        a[i] = (int32_t)((i * 13u) & 0x0000FFFFu); // keep small magnitude
        b[i] = (int32_t)(((n - i) * 7u) & 0x0000FFFFu);
    }
}

static int64_t dot_cpu(const int32_t *a, const int32_t *b, uint32_t n, uint32_t rshift) {
    int64_t acc = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t p = (int64_t)a[i] * (int64_t)b[i];
        acc += (p >> rshift);
    }
    return acc;
}

static void conv1d_cpu(const int32_t *x, const int32_t *k, int32_t *y, uint32_t out_len, uint32_t klen, uint32_t rshift) {
    for (uint32_t o = 0; o < out_len; o++) {
        int64_t acc = 0;
        for (uint32_t j = 0; j < klen; j++) {
            acc += ((int64_t)x[o + j] * (int64_t)k[j]) >> rshift;
        }
        y[o] = (int32_t)acc;
    }
}

static void bench_dot(uint32_t n, uint32_t rshift) {
    static int32_t A[2048];
    static int32_t B[2048];
    fill_inputs((uint32_t *)A, (uint32_t *)B, n);

    uint64_t c0 = rdcycle();
    int64_t cpu_res = dot_cpu(A, B, n, rshift);
    uint64_t c1 = rdcycle();
    uint64_t cpu_cycles = c1 - c0;

    // Load accelerator memories
    for (uint32_t i = 0; i < n; i++) {
        *memA_ptr(i) = (uint32_t)A[i];
        *memB_ptr(i) = (uint32_t)B[i];
    }

    accel_clear();
    accel_config(n, 0u, rshift);
    accel_start(false);
    accel_wait_done();

    uint64_t acc_cycles = ((uint64_t)(*reg_ptr(REG_CYCLES_HI)) << 32) | (uint64_t)(*reg_ptr(REG_CYCLES_LO));
    int32_t accel_res = (int32_t)(*memO_ptr(0));

    double ops = (double)n; // MACs
    double tput_cpu = ops / (double)cpu_cycles;
    double tput_acc = ops / (double)acc_cycles;
    double energy_cpu = (double)cpu_cycles; // unit energy per cycle (est.)
    double energy_acc = (double)acc_cycles;
    double speedup = (double)cpu_cycles / (double)acc_cycles;

    printf("BENCH, dot, n=%u, rshift=%u, cpu_cycles=%llu, acc_cycles=%llu, speedup=%.3f\n",
           (unsigned)n, (unsigned)rshift,
           (unsigned long long)cpu_cycles, (unsigned long long)acc_cycles, speedup);
    printf("RESULT, cpu=%lld, accel=%ld\n", (long long)cpu_res, (long)accel_res);
    printf("THROUGHPUT, cpu=%.6f MAC/cyc, accel=%.6f MAC/cyc\n", tput_cpu, tput_acc);
    printf("ENERGY_EST, cpu=%.0f, accel=%.0f (arb units)\n", energy_cpu, energy_acc);
}

static void bench_conv(uint32_t out_len, uint32_t klen, uint32_t rshift) {
    static int32_t X[4096];
    static int32_t K[512];
    static int32_t Y[4096];
    // Fill input X with out_len + klen - 1 samples (CPU will index up to o + k - 1)
    fill_inputs((uint32_t *)X, (uint32_t *)K, out_len + klen);

    uint64_t c0 = rdcycle();
    conv1d_cpu(X, K, Y, out_len, klen, rshift);
    uint64_t c1 = rdcycle();
    uint64_t cpu_cycles = c1 - c0;

    // Load accelerator memories: X into A, K into B
    for (uint32_t i = 0; i < out_len + klen; i++) {
        *memA_ptr(i) = (uint32_t)X[i];
    }
    for (uint32_t j = 0; j < klen; j++) {
        *memB_ptr(j) = (uint32_t)K[j];
    }

    accel_clear();
    accel_config(out_len, klen, rshift);
    accel_start(true);
    accel_wait_done();

    uint64_t acc_cycles = ((uint64_t)(*reg_ptr(REG_CYCLES_HI)) << 32) | (uint64_t)(*reg_ptr(REG_CYCLES_LO));

    // Pull a couple of outputs for sanity
    int32_t acc_y0 = (int32_t)(*memO_ptr(0));
    int32_t acc_y1 = (int32_t)(*memO_ptr(1));

    double ops = (double)out_len * (double)klen; // MACs
    double tput_cpu = ops / (double)cpu_cycles;
    double tput_acc = ops / (double)acc_cycles;
    double energy_cpu = (double)cpu_cycles; // unit energy per cycle (est.)
    double energy_acc = (double)acc_cycles;
    double speedup = (double)cpu_cycles / (double)acc_cycles;

    printf("BENCH, conv, out_len=%u, klen=%u, rshift=%u, cpu_cycles=%llu, acc_cycles=%llu, speedup=%.3f\n",
           (unsigned)out_len, (unsigned)klen, (unsigned)rshift,
           (unsigned long long)cpu_cycles, (unsigned long long)acc_cycles, speedup);
    printf("RESULT, y0_cpu=%ld, y1_cpu=%ld, y0_acc=%ld, y1_acc=%ld\n",
           (long)Y[0], (long)Y[1], (long)acc_y0, (long)acc_y1);
    printf("THROUGHPUT, cpu=%.6f MAC/cyc, accel=%.6f MAC/cyc\n", tput_cpu, tput_acc);
    printf("ENERGY_EST, cpu=%.0f, accel=%.0f (arb units)\n", energy_cpu, energy_acc);
}

static void print_menu(void) {
    puts("\n=== DSP Accelerator Demo ===");
    puts("1) Dot-product benchmark (n=1024, shift=15)");
    puts("2) 1D Convolution benchmark (out_len=512, klen=64, shift=15)");
    puts("q) Quit");
    printf("> ");
}

int main(void) {
    puts("RISC-V SoC + Vector MAC/Conv Accelerator");
    puts("Mapped at ACCEL_BASE=0x30000000\n");

    while (1) {
        print_menu();
        int c = getchar();
        if (c == '1') {
            bench_dot(1024u, 15u);
        } else if (c == '2') {
            bench_conv(512u, 64u, 15u);
        } else if (c == 'q' || c == 'Q') {
            puts("Bye.");
            break;
        }
    }
    return 0;
}


