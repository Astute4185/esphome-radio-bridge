#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
PYTEST_DISABLE_PLUGIN_AUTOLOAD=1 python -m pytest -q tests/python
BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$BUILD_DIR"' EXIT
for name in test_mode_and_ring test_uart_modes test_bridge_health test_radio_identification test_firmware_catalogue; do
  "${CXX:-g++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
    -Icomponents/esphome-radio-bridge \
    "tests/cpp/${name}.cpp" -o "$BUILD_DIR/$name"
  "$BUILD_DIR/$name"
done
echo "Host C++ tests passed."
