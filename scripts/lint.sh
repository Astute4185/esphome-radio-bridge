#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
python -m ruff check .
python -m ruff format --check .
python -m yamllint -c .yamllint.yaml examples packages tests/components .github
if ! command -v clang-format >/dev/null 2>&1; then
  echo "clang-format is required: install it with your system package manager" >&2
  exit 1
fi
find components tests/cpp -type f \( -name '*.cpp' -o -name '*.h' \) -print0 |
  xargs -0 clang-format --dry-run --Werror
bash -n scripts/lint.sh scripts/test.sh scripts/validate.sh
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git diff --check
fi
