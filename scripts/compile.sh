#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

# Config validation alone does not compile C++ or reveal missing headers.
./scripts/validate.sh

# Unit-test fixture covers the component; full example covers network/HA integrations.
esphome compile tests/components/esphome-radio-bridge/test.esp32-idf.yaml
esphome compile examples/tubeszb-mgm210-poe-2023.yaml
