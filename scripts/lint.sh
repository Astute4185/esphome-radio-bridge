#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'USAGE'
Usage: ./scripts/lint.sh [--fix]

  (no args)  Check formatting and lint rules without modifying files.
  --fix      Apply Ruff and clang-format fixes, then run remaining checks.
  -h, --help Show this help.

YAML linting and shell syntax checking remain check-only; yamllint does not
support automatic fixes.
USAGE
}

fix=false
case "${1:-}" in
  '') ;;
  --fix) fix=true ;;
  -h|--help) usage; exit 0 ;;
  *) echo "Invalid option: $1" >&2; usage >&2; exit 2 ;;
esac
if (( $# > 1 )); then
  echo "Expected at most one option." >&2
  usage >&2
  exit 2
fi

cd "$(dirname "${BASH_SOURCE[0]}")/.."

if "$fix"; then
  python -m ruff check --fix .
  python -m ruff format .
else
  python -m ruff check .
  python -m ruff format --check .
fi

# yamllint is validation-only; no native --fix mode.
yaml_paths=()
for path in examples packages tests/components .github; do
  [[ ! -d "$path" ]] || yaml_paths+=("$path")
done
if ((${#yaml_paths[@]})); then
  python -m yamllint -c .yamllint.yaml "${yaml_paths[@]}"
fi

cxx_roots=()
for path in components tests/cpp; do
  [[ ! -d "$path" ]] || cxx_roots+=("$path")
done
if ((${#cxx_roots[@]})); then
  if ! command -v clang-format >/dev/null 2>&1; then
    echo 'clang-format is required; install it to format/check C++ files.' >&2
    exit 1
  fi
  mapfile -d '' cxx_files < <(find "${cxx_roots[@]}" -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.hpp' \) -print0)
  if ((${#cxx_files[@]})); then
    if "$fix"; then
      clang-format -i "${cxx_files[@]}"
    else
      clang-format --dry-run --Werror "${cxx_files[@]}"
    fi
  fi
fi

bash_files=(scripts/*.sh)
if ((${#bash_files[@]})); then
  bash -n "${bash_files[@]}"
fi

if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git diff --check
fi