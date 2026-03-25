#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

mapfile -t FILES < <(
  cd "${ROOT_DIR}" &&
    rg --files include src tests examples benchmarks \
      -g '*.h' -g '*.hh' -g '*.hpp' -g '*.cc' -g '*.cpp'
)

if [[ "${#FILES[@]}" -eq 0 ]]; then
  echo "No source files found."
  exit 0
fi

for index in "${!FILES[@]}"; do
  FILES[$index]="${ROOT_DIR}/${FILES[$index]}"
done

clang-format -i "${FILES[@]}"
