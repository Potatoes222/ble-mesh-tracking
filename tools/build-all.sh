#!/usr/bin/env bash
# Build the four firmware projects sequentially with ESP-IDF.
# Requires $IDF_PATH/export.sh to have been sourced first.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py not on PATH. Source the ESP-IDF export script first:" >&2
    echo "  . \$IDF_PATH/export.sh" >&2
    exit 1
fi

apps=(gateway relay scanner tag)
for app in "${apps[@]}"; do
    echo "==> Build apps/$app"
    (cd "apps/$app" && idf.py build)
done

echo "All $(( ${#apps[@]} )) projects built."
