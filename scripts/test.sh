#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python -m pytest -q tests/python
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
for name in test_mode_and_ring test_uart_modes; do
  "${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -Icomponents/serial_radio_gateway \
    "tests/cpp/${name}.cpp" -o "$BUILD_DIR/$name"
  "$BUILD_DIR/$name"
done
echo "Host C++ tests passed."
