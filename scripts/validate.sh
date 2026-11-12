#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if ! command -v esphome >/dev/null 2>&1; then
  echo "ESPHome CLI not installed. Install esphome==2026.10.1 to validate examples." >&2
  exit 1
fi
if [ ! -f examples/secrets.yaml ]; then
  cp examples/secrets.example.yaml examples/secrets.yaml
  echo "Created examples/secrets.yaml with a NON-PRODUCTION testing key."
fi
esphome config tests/components/esphome-radio-bridge/test.esp32-idf.yaml
esphome config examples/tubeszb-minimal.yaml
esphome config examples/tubeszb-mgm210-poe-2023.yaml
if [ "${COMPILE:-0}" = "1" ]; then
  esphome compile tests/components/esphome-radio-bridge/test.esp32-idf.yaml
  esphome compile examples/tubeszb-mgm210-poe-2023.yaml
fi
