#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

if [[ "${COMPILE:-0}" != "0" ]]; then
  echo "COMPILE=1 is obsolete; use ./scripts/compile.sh for a full build." >&2
  exit 2
fi

if ! command -v esphome >/dev/null 2>&1; then
  echo "ESPHome CLI is required (tested with ESPHome 2026.10.2)." >&2
  exit 1
fi

# A real 32-byte random key is required for ESPHome >=2026.10.2.
# This file is ignored by git and is never generated in a tracked location.
if [[ ! -f examples/secrets.yaml ]]; then
  api_key="$(python -c 'import base64, secrets; print(base64.b64encode(secrets.token_bytes(32)).decode())')"
  cat > examples/secrets.yaml <<EOF
apikey_tubeszb: "${api_key}"
web_server_username: "test-admin"
web_server_password: "CHANGE-ME-BEFORE-DEPLOYING"
EOF
  echo "Created ignored examples/secrets.yaml with a randomly generated API key."
fi

esphome config tests/components/esphome-radio-bridge/test.esp32-idf.yaml
esphome config examples/tubeszb-minimal.yaml
esphome config examples/tubeszb-mgm210-poe-2023.yaml
