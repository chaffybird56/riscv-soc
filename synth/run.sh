#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
cd "${ROOT_DIR}/synth"

mkdir -p results reports tmp

echo "[yosys] Synthesizing accelerator"
yosys -ql reports/yosys.log -s yosys.ys

if command -v openroad >/dev/null 2>&1; then
  echo "[openroad] Running minimal PnR/QoR"
  openroad -exit openroad.tcl | tee reports/openroad.log
else
  echo "[openroad] Not found. Skipping PnR. Set TECH_LEF/LIB_LEF/LIB_LIB and install OpenROAD."
fi

echo "[done] See synth/reports and synth/results"


