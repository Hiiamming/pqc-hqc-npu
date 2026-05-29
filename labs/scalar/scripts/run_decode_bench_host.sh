#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"

HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}"
case "$HQC_PARAM_LEVEL" in
    128) PARAM_SET=1 ;;
    192) PARAM_SET=3 ;;
    256) PARAM_SET=5 ;;
    *) echo "ERROR: HQC_PARAM_LEVEL must be 128, 192, or 256" >&2; exit 1 ;;
esac

if [ ! -f "$SHARED_DIR/fixtures/hqc${PARAM_SET}_decode_fixture.c" ]; then
    HQC_PARAM_LEVEL="$HQC_PARAM_LEVEL" "$SCRIPT_DIR/gen_decode_fixture.sh"
fi

BENCH_ITERS="${HQC_BENCH_ITERS:-100}"
OUT="$PROJECT_DIR/build/hqc${PARAM_SET}_decode_bench_host"
mkdir -p "$(dirname "$OUT")"

echo "=== Compiling HQC-$HQC_PARAM_LEVEL scalar host benchmark, iters=$BENCH_ITERS ==="
gcc -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
    -DHQC_BENCH_ITERS="$BENCH_ITERS" \
    -I "$SHARED_DIR/fixtures" \
    -I "$SHARED_DIR/src/common" \
    -I "$PROJECT_DIR/src/common" \
    -I "$SHARED_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-${PARAM_SET}" \
    "$PROJECT_DIR/demos/hqc${PARAM_SET}_decode_bench.c" \
    "$SHARED_DIR/fixtures/hqc${PARAM_SET}_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -Wl,--gc-sections \
    -o "$OUT"
echo "  -> $OUT"

echo
echo "=== Running on host ==="
"$OUT"
