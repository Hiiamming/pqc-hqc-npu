#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ ! -f "$PROJECT_DIR/fixtures/hqc256_decode_fixture.c" ]; then
    "$SCRIPT_DIR/gen_hqc256_decode_fixture.sh"
fi

OUT="$PROJECT_DIR/build/hqc256_decode_bench_host"
mkdir -p "$(dirname "$OUT")"

gcc -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-5" \
    "$PROJECT_DIR/demos/hqc256_decode_bench.c" \
    "$PROJECT_DIR/fixtures/hqc256_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -Wl,--gc-sections \
    -o "$OUT"

"$OUT"
