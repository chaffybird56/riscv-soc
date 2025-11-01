#!/usr/bin/env python3
import argparse
import re
from collections import defaultdict
import matplotlib.pyplot as plt


def parse_log(path):
    benches = {}
    pat = re.compile(r"BENCH,\s*(\w+),.*?cpu_cycles=(\d+),\s*acc_cycles=(\d+),\s*speedup=([0-9.]+)")
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            m = pat.search(line)
            if m:
                name = m.group(1)
                cpu = int(m.group(2))
                acc = int(m.group(3))
                spd = float(m.group(4))
                benches[name] = {"cpu_cycles": cpu, "acc_cycles": acc, "speedup": spd}
    return benches


def plot_bars(benches, out_path):
    labels = []
    cpu_vals = []
    acc_vals = []
    for name, m in benches.items():
        labels.append(name)
        cpu_vals.append(m["cpu_cycles"])
        acc_vals.append(m["acc_cycles"])

    x = range(len(labels))
    width = 0.35
    fig, ax = plt.subplots(figsize=(6, 4))
    ax.bar([i - width / 2 for i in x], cpu_vals, width, label="CPU cycles")
    ax.bar([i + width / 2 for i in x], acc_vals, width, label="Accelerator cycles")
    ax.set_ylabel("Cycles")
    ax.set_title("CPU vs Accelerator")
    ax.set_xticks(list(x))
    ax.set_xticklabels(labels)
    ax.legend()
    ax.grid(True, axis="y", linestyle=":", alpha=0.6)
    fig.tight_layout()
    fig.savefig(out_path, dpi=180)
    print(f"[plot] Wrote {out_path}")


def main():
    ap = argparse.ArgumentParser(description="Plot CPU vs accelerator bar charts from log")
    ap.add_argument("--log", required=True, help="Log file path from firmware output")
    ap.add_argument("--out", required=True, help="Output image path")
    args = ap.parse_args()

    benches = parse_log(args.log)
    if not benches:
        raise SystemExit("No BENCH lines found in log")
    plot_bars(benches, args.out)


if __name__ == "__main__":
    main()


