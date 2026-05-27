#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT_DIR="$(cd "$PROJECT_DIR/../.." && pwd)"
SCALAR_DIR="$ROOT_DIR/labs/scalar"

bash "$SCALAR_DIR/scripts/gen_hqc5_decode_fixture.sh"

mkdir -p "$PROJECT_DIR/fixtures"
cp "$SCALAR_DIR/fixtures/hqc5_decode_fixture.c" "$PROJECT_DIR/fixtures/hqc5_decode_fixture.c"
cp "$SCALAR_DIR/fixtures/hqc5_decode_fixture.h" "$PROJECT_DIR/fixtures/hqc5_decode_fixture.h"
echo "Generated $PROJECT_DIR/fixtures/hqc5_decode_fixture.c from labs/scalar"
