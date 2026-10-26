#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"
CONFIG="${CONFIG:-Release}"
OUT="${1:-$ROOT/dist/glimiter-ui.png}"
SHOT="$BUILD_DIR/GLimiterScreenshot_artefacts/$CONFIG/GLimiter Screenshot"

mkdir -p "$(dirname "$OUT")"

if [ -x "$SHOT" ]; then
    SHOT="$SHOT"
else
    # Fall back to the non-spaced artefact name if the generator differed.
    SHOT="$BUILD_DIR/GLimiterScreenshot_artefacts/$CONFIG/GLimiterScreenshot"
fi

if [ ! -x "$SHOT" ]; then
    echo "Screenshot binary not found. Run scripts/build.sh first." >&2
    exit 1
fi

if command -v xvfb-run >/dev/null 2>&1; then
    xvfb-run -a "$SHOT" --out="$OUT"
else
    "$SHOT" --out="$OUT"
fi

echo "Wrote $OUT"
