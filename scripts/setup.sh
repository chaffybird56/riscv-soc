#!/usr/bin/env bash
set -euo pipefail

# Basic environment setup for macOS/Linux
# - Creates virtualenv
# - Installs Python deps
# - Attempts to install LiteX + litex-boards from git (if not already present)

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PYTHON=${PYTHON:-python3}

echo "[setup] Project root: ${PROJECT_ROOT}"

if [ ! -d "${PROJECT_ROOT}/.venv" ]; then
  echo "[setup] Creating virtualenv (.venv)"
  ${PYTHON} -m venv "${PROJECT_ROOT}/.venv"
fi

source "${PROJECT_ROOT}/.venv/bin/activate"
pip install --upgrade pip

echo "[setup] Installing Python dependencies"
pip install numpy matplotlib pyserial

echo "[setup] Installing LiteX + Migen + litex-boards from git (if not present)"
pip install --upgrade git+https://github.com/enjoy-digital/migen.git
pip install --upgrade git+https://github.com/enjoy-digital/litex.git
pip install --upgrade git+https://github.com/litex-hub/litex-boards.git

echo "[setup] Done. Activate with: source ${PROJECT_ROOT}/.venv/bin/activate"


