#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

OUT="$PROJECT_DIR/build/gen_hqc128_decode_fixture"
FIXTURE="$PROJECT_DIR/fixtures/hqc128_decode_fixture.c"
mkdir -p "$(dirname "$OUT")" "$(dirname "$FIXTURE")"

gcc -std=c11 -O2 -Wall -Wextra \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-1" \
    "$PROJECT_DIR/tools/gen_hqc128_decode_fixture.c" \
    "$PROJECT_DIR/src/common/code.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -o "$OUT"

"$OUT" > "$FIXTURE"
echo "Generated $FIXTURE"
