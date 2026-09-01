#!/usr/bin/env bash
# Rebuild the PebbleKit JS bundle: src/ts/*.ts -> src/ts-build/index.js.
#
# `pebble build` does NOT run this automatically (see package.json's own
# _note) -- run this after editing anything under src/ts/, then `pebble
# build` as usual.
#
# Usage: ./build_ts.sh

set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$PROJECT_DIR"

echo "Compiling..."
npm run build:ts

echo "Wrote $PROJECT_DIR/src/ts-build/index.js"
echo "Run 'pebble build' next."
