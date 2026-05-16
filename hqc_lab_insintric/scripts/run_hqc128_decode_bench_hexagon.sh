#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
TUTORIAL_ROOT="${HEXAGON_TUTORIAL_ROOT:-$(cd "$PROJECT_DIR/.." && pwd)}"
HEXAGON_SDK_ROOT="${HEXAGON_SDK_ROOT:-$TUTORIAL_ROOT/tools/hexagon-sdk}"

TOOLS_BIN="$HEXAGON_SDK_ROOT/tools/HEXAGON_Tools/19.0.04/Tools/bin"
H2_INSTALL="$TUTORIAL_ROOT/tools/h2-install"
H2_KERNEL="$TUTORIAL_ROOT/tools/hexagon-hypervisor/kernel/include"

if [ -L "$H2_INSTALL" ]; then
    H2_ROOT="$(dirname "$(readlink -f "$H2_INSTALL")")"
    H2_KERNEL="$H2_ROOT/kernel/include"
fi

CLANG="$TOOLS_BIN/hexagon-clang"
SIM="$TOOLS_BIN/hexagon-sim"
BOOTER="$H2_INSTALL/bin/booter"

for f in "$CLANG" "$SIM" "$BOOTER"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run install_tools.sh first or set HEXAGON_TUTORIAL_ROOT/HEXAGON_SDK_ROOT."
        exit 1
    fi
done

if [ ! -f "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" ]; then
    "$SCRIPT_DIR/gen_hqc128_decode_fixture.sh"
fi

OUT="$PROJECT_DIR/build/hqc128_decode_bench_hexagon"
BENCH_ITERS="${HQC128_BENCH_ITERS:-100}"
GF_LUT_MUL="${HQC_GF_LUT_MUL:-0}"
GF_HWSTYLE_MUL="${HQC_GF_HWSTYLE_MUL:-1}"
HVX_RS_SYNDROME="${HQC_HVX_RS_SYNDROME:-1}"
RS_FAST_NON_CT="${HQC_RS_FAST_NON_CT:-0}"
RS_ROOTS_FFT="${HQC_RS_ROOTS_FFT:-0}"
RS_ROOTS_HVX="${HQC_RS_ROOTS_HVX:-0}"
RM_EXPAND_LUT="${HQC_RM_EXPAND_LUT:-0}"
RM_FUSED_FAST="${HQC_RM_FUSED_FAST:-0}"
GF_LUT_FLAGS=()
if [ "$GF_LUT_MUL" = "1" ]; then
    GF_LUT_FLAGS=(-DHQC_USE_GF_LUT_MUL=1)
fi
GF_HWSTYLE_FLAGS=()
if [ "$GF_HWSTYLE_MUL" = "1" ] && [ "$GF_LUT_MUL" != "1" ]; then
    GF_HWSTYLE_FLAGS=(-DHQC_USE_GF_HWSTYLE_MUL=1)
fi
HVX_RS_SYNDROME_FLAGS=()
if [ "$HVX_RS_SYNDROME" = "1" ]; then
    HVX_RS_SYNDROME_FLAGS=(-DHQC_USE_HVX_RS_SYNDROME=1)
fi
RS_FAST_FLAGS=()
if [ "$RS_FAST_NON_CT" = "1" ]; then
    RS_FAST_FLAGS=(-DHQC_RS_FAST_NON_CT=1)
fi
RS_ROOTS_FLAGS=()
if [ "$RS_ROOTS_FFT" = "1" ]; then
    RS_ROOTS_FLAGS=(-DHQC_RS_ROOTS_FFT=1)
fi
RS_ROOTS_HVX_FLAGS=()
if [ "$RS_ROOTS_HVX" = "1" ]; then
    RS_ROOTS_HVX_FLAGS=(-DHQC_RS_ROOTS_HVX=1)
fi
RM_EXPAND_FLAGS=()
if [ "$RM_EXPAND_LUT" = "1" ]; then
    RM_EXPAND_FLAGS=(-DHQC_RM_EXPAND_LUT=1)
fi
RM_FUSED_FLAGS=()
if [ "$RM_FUSED_FAST" = "1" ]; then
    RM_FUSED_FLAGS=(-DHQC_RM_FUSED_FAST=1)
fi
mkdir -p "$(dirname "$OUT")"

echo "=== Compiling HQC-128 decode benchmark for Hexagon HVX intrinsic, iters=$BENCH_ITERS, gf_lut=$GF_LUT_MUL, gf_hwstyle=$GF_HWSTYLE_MUL, hvx_rs_syndrome=$HVX_RS_SYNDROME, rs_fast_non_ct=$RS_FAST_NON_CT, rs_roots_fft=$RS_ROOTS_FFT, rs_roots_hvx=$RS_ROOTS_HVX, rm_expand_lut=$RM_EXPAND_LUT, rm_fused_fast=$RM_FUSED_FAST ==="
"$CLANG" -O2 -mv75 \
    -ffunction-sections -fdata-sections \
    -mhvx -mhvx-length=128B \
    -mhmx \
    -DHQC_USE_HVX_INTRINSICS=1 \
    "${GF_LUT_FLAGS[@]}" \
    "${GF_HWSTYLE_FLAGS[@]}" \
    "${HVX_RS_SYNDROME_FLAGS[@]}" \
    "${RS_FAST_FLAGS[@]}" \
    "${RS_ROOTS_FLAGS[@]}" \
    "${RS_ROOTS_HVX_FLAGS[@]}" \
    "${RM_EXPAND_FLAGS[@]}" \
    "${RM_FUSED_FLAGS[@]}" \
    -DARCHV=75 \
    -DHQC128_BENCH_ITERS="$BENCH_ITERS" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-1" \
    -I "$H2_INSTALL/include" \
    -I "$H2_KERNEL" \
    -moslib=h2 \
    -Wl,-L,"$H2_INSTALL/lib" \
    -Wl,--section-start=.start=0x02000000 \
    -Wl,--gc-sections \
    -o "$OUT" \
    "$PROJECT_DIR/demos/hqc128_decode_bench.c" \
    "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c"
echo "  -> $OUT"

echo
echo "=== Running on hexagon-sim ==="
"$SIM" --mv75 --mhmx 1 --simulated_returnval \
    -- "$BOOTER" \
    --ext_power 1 \
    --use_ext 1 \
    --fence_hi 0xfe000000 \
    "$OUT"
