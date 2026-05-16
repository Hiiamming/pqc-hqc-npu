#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

if [ ! -f "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" ]; then
    "$SCRIPT_DIR/gen_hqc128_decode_fixture.sh"
fi

OUT="$PROJECT_DIR/build/hqc128_decode_bench_host"
GF_LUT_MUL="${HQC_GF_LUT_MUL:-0}"
GF_HWSTYLE_MUL="${HQC_GF_HWSTYLE_MUL:-1}"
RS_FAST_NON_CT="${HQC_RS_FAST_NON_CT:-0}"
RS_ROOTS_FFT="${HQC_RS_ROOTS_FFT:-0}"
GF_FLAGS=()
if [ "$GF_LUT_MUL" = "1" ]; then
    GF_FLAGS=(-DHQC_USE_GF_LUT_MUL=1)
fi
if [ "$GF_HWSTYLE_MUL" = "1" ] && [ "$GF_LUT_MUL" != "1" ]; then
    GF_FLAGS+=(-DHQC_USE_GF_HWSTYLE_MUL=1)
fi
RS_FAST_FLAGS=()
if [ "$RS_FAST_NON_CT" = "1" ]; then
    RS_FAST_FLAGS=(-DHQC_RS_FAST_NON_CT=1)
fi
RS_ROOTS_FLAGS=()
if [ "$RS_ROOTS_FFT" = "1" ]; then
    RS_ROOTS_FLAGS=(-DHQC_RS_ROOTS_FFT=1)
fi
mkdir -p "$(dirname "$OUT")"

gcc -std=c11 -O2 -Wall -Wextra -ffunction-sections -fdata-sections \
    "${GF_FLAGS[@]}" \
    "${RS_FAST_FLAGS[@]}" \
    "${RS_ROOTS_FLAGS[@]}" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-1" \
    "$PROJECT_DIR/demos/hqc128_decode_bench.c" \
    "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -Wl,--gc-sections \
    -o "$OUT"

"$OUT"
