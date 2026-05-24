#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROJECT_DIR="$ROOT_DIR/labs/scalar"

CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}"
case "$HQC_PARAM_LEVEL" in
    128)
        PARAM_DIR="hqc-1"
        FIXTURE_PREFIX="hqc128"
        BENCH_ITERS="${HQC128_BENCH_ITERS:-${HQC_BENCH_ITERS:-1000}}"
        ;;
    192)
        PARAM_DIR="hqc-192"
        FIXTURE_PREFIX="hqc192"
        BENCH_ITERS="${HQC192_BENCH_ITERS:-${HQC_BENCH_ITERS:-1000}}"
        ;;
    256)
        PARAM_DIR="hqc-256"
        FIXTURE_PREFIX="hqc256"
        BENCH_ITERS="${HQC256_BENCH_ITERS:-${HQC_BENCH_ITERS:-1000}}"
        ;;
    *)
        echo "ERROR: HQC_PARAM_LEVEL must be 128, 192, or 256" >&2
        exit 1
        ;;
esac
OUT_DIR="$SCRIPT_DIR/build"
OUT="$OUT_DIR/hqc${HQC_PARAM_LEVEL}_decode_bench_arm64"

if ! command -v "$CC" >/dev/null 2>&1; then
    echo "ERROR: $CC not found. Install gcc-aarch64-linux-gnu or set AARCH64_CC." >&2
    exit 1
fi

if [ ! -f "$PROJECT_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" ]; then
    "$PROJECT_DIR/scripts/gen_${FIXTURE_PREFIX}_decode_fixture.sh"
fi

mkdir -p "$OUT_DIR"

echo "=== Building HQC-$HQC_PARAM_LEVEL scalar decode baseline for ARM64, iters=$BENCH_ITERS ==="
"$CC" -std=c11 -O2 -Wall -Wextra \
    -ffunction-sections -fdata-sections \
    -DHQC_PARAM_LEVEL="$HQC_PARAM_LEVEL" \
    -DHQC${HQC_PARAM_LEVEL}_BENCH_ITERS="$BENCH_ITERS" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/$PARAM_DIR" \
    "$SCRIPT_DIR/hqc_decode_bench_arm64.c" \
    "$PROJECT_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -Wl,--gc-sections \
    -o "$OUT"

file "$OUT"
echo "  -> $OUT"
