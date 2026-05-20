#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_DIR="$ROOT_DIR/hqc_lab_scalar"

CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
BENCH_ITERS="${HQC128_BENCH_ITERS:-1000}"
OUT_DIR="$SCRIPT_DIR/build"
OUT="$OUT_DIR/hqc128_decode_bench_arm64"

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "ERROR: $CC not found. Install gcc-aarch64-linux-gnu or set AARCH64_CC." >&2
    exit 1
fi

if [ ! -f "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" ]; then
    "$PROJECT_DIR/scripts/gen_hqc128_decode_fixture.sh"
fi

mkdir -p "$OUT_DIR"

echo "=== Building HQC-128 scalar decode baseline for ARM64, iters=$BENCH_ITERS ==="
"$CC" -std=c11 -O2 -Wall -Wextra \
    -ffunction-sections -fdata-sections \
    -DHQC128_BENCH_ITERS="$BENCH_ITERS" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-1" \
    "$SCRIPT_DIR/hqc128_decode_bench_arm64.c" \
    "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -Wl,--gc-sections \
    -o "$OUT"

file "$OUT"
echo "  -> $OUT"
