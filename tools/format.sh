#!/usr/bin/env bash
# Run clang-format across every firmware source file using the root
# .clang-format.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v clang-format >/dev/null 2>&1; then
    echo "clang-format not on PATH. Install first:" >&2
    echo "  Ubuntu: sudo apt install clang-format" >&2
    echo "  macOS:  brew install clang-format" >&2
    exit 1
fi

files=$(find apps -type f \( -name '*.c' -o -name '*.h' \))
if [ -z "$files" ]; then
    echo "No .c / .h files found under apps/"
    exit 0
fi

echo "$files" | xargs clang-format -i
echo "Formatted $(echo "$files" | wc -l) file(s)."
