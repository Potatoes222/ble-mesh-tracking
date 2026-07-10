#!/usr/bin/env bash
# Chay clang-format tren toan bo source firmware theo .clang-format o repo root.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format khong co san. Cai truoc khi chay:" >&2
    echo "  Ubuntu: sudo apt install clang-format" >&2
    echo "  macOS:  brew install clang-format" >&2
    exit 1
fi

files=$(find apps -type f \( -name '*.c' -o -name '*.h' \))
if [ -z "$files" ]; then
    echo "Khong tim thay file .c/.h nao trong apps/"
    exit 0
fi

echo "$files" | xargs clang-format -i
echo "Da format $(echo "$files" | wc -l) file."
