#!/usr/bin/env bash
# Build-only system dependencies for PlatformIO (espressif32 / Arduino).
# No upload tooling: no TTY is passed through, so udev rules, libusb,
# dfu-util, dialout groups, etc. are intentionally omitted.
set -eux

export DEBIAN_FRONTEND=noninteractive

# Root-safe: works as root in `docker build` and as vscode user with sudo.
if [ "$(id -u)" -eq 0 ]; then
  SUDO=""
else
  SUDO="sudo"
fi

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
  python3 python3-pip python3-venv \
  git curl ca-certificates unzip tar xz-utils \
  build-essential cmake ninja-build pkg-config ccache

# Sanity check: fail fast if anything essential is still missing.
python3 --version
pip3 --version
git --version
cmake --version
ninja --version
pkg-config --version
