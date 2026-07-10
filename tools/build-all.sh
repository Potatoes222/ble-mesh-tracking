#!/usr/bin/env bash
# Build lan luot 4 project firmware bang ESP-IDF.
# Yeu cau: da source $IDF_PATH/export.sh truoc khi chay.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py khong co san. Source ESP-IDF export.sh truoc khi chay:" >&2
    echo "  . \$IDF_PATH/export.sh" >&2
    exit 1
fi

apps=(gateway relay scanner tag)
for app in "${apps[@]}"; do
    echo "==> Build apps/$app"
    (cd "apps/$app" && idf.py build)
done

echo "Tat ca $(( ${#apps[@]} )) project build xong."
