#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
SHARED_DIR="$ROOT_DIR/shared"
PROJECT_DIR="${HQC_PROJECT_DIR:-$ROOT_DIR/labs/fastest}"
HEXAGON_SDK_ROOT="${HEXAGON_SDK_ROOT:-$ROOT_DIR/../tools/hexagon-sdk}"

QAIC="${QAIC:-$HEXAGON_SDK_ROOT/ipc/fastrpc/qaic/Ubuntu/qaic}"
HEXAGON_CLANG="${HEXAGON_CLANG:-$HEXAGON_SDK_ROOT/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-clang}"
AARCH64_CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
FASTRPC_LIB_DIR="${FASTRPC_LIB_DIR:-$HEXAGON_SDK_ROOT/ipc/fastrpc/remote/ship/UbuntuARM_aarch64}"
AARCH64_LDFLAGS="${AARCH64_LDFLAGS:-}"
TESTSIG="$HEXAGON_SDK_ROOT/tools/elfsigner/output/testsig-0xaa3ec42e.so"

BUILD_DIR="$SCRIPT_DIR/build"
GEN_DIR="$SCRIPT_DIR/generated"
HQC_RS_FAST_NON_CT="${HQC_RS_FAST_NON_CT:-0}"
HQC_GF_LUT_MUL="${HQC_GF_LUT_MUL:-0}"
HQC_RM_EXPAND_LUT="${HQC_RM_EXPAND_LUT:-0}"
HQC_RM_FUSED_FAST="${HQC_RM_FUSED_FAST:-0}"
HQC_RS_ROOTS_HVX="${HQC_RS_ROOTS_HVX:-1}"

HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}"
case "$HQC_PARAM_LEVEL" in
    128)
        PARAM_DIR="hqc-1"
        FIXTURE_PREFIX="hqc1"
        DEFAULT_ITERS_DEFINE="-DHQC_DEFAULT_BENCH_ITERS=1000"
        ;;
    192)
        PARAM_DIR="hqc-3"
        FIXTURE_PREFIX="hqc3"
        DEFAULT_ITERS_DEFINE="-DHQC_DEFAULT_BENCH_ITERS=100"
        ;;
    256)
        PARAM_DIR="hqc-5"
        FIXTURE_PREFIX="hqc5"
        DEFAULT_ITERS_DEFINE="-DHQC_DEFAULT_BENCH_ITERS=50"
        ;;
    *)
        echo "ERROR: HQC_PARAM_LEVEL must be 128, 192, or 256" >&2
        exit 1
        ;;
esac

for tool in "$QAIC" "$HEXAGON_CLANG" "$AARCH64_CC"; do
    if ! command -v "$tool" >/dev/null 2>&1 && [ ! -x "$tool" ]; then
        echo "ERROR: $tool not found" >&2
        exit 1
    fi
done

if [ ! -f "$SHARED_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" ]; then
    "$PROJECT_DIR/scripts/gen_${FIXTURE_PREFIX}_decode_fixture.sh"
fi

mkdir -p "$BUILD_DIR" "$GEN_DIR"

echo "=== Generating FastRPC stub/skel ==="
(
    cd "$GEN_DIR"
    "$QAIC" -I "$HEXAGON_SDK_ROOT/incs" "$SCRIPT_DIR/hqc.idl"
)

HEXAGON_ARCH="${HEXAGON_ARCH:-v68}"
case "$HEXAGON_ARCH" in
    v[0-9]*)
        HEXAGON_ARCH_FLAG="-m$HEXAGON_ARCH"
        ;;
    *)
        echo "ERROR: HEXAGON_ARCH must look like v68, v73, v75, ..." >&2
        exit 1
        ;;
esac
echo "=== Building HQC-$HQC_PARAM_LEVEL cDSP HVX intrinsic skel, arch=$HEXAGON_ARCH ==="
common_sources=()
if [ -f "$PROJECT_DIR/src/common/fft.c" ]; then
    common_sources+=("$PROJECT_DIR/src/common/fft.c")
fi
"$HEXAGON_CLANG" -O2 -fPIC -shared \
    "$HEXAGON_ARCH_FLAG" \
    -mhvx -mhvx-length=128B \
    -DHQC_PARAM_LEVEL="$HQC_PARAM_LEVEL" \
    -DHQC_USE_HVX_INTRINSICS=1 \
    -DHQC_USE_HVX_RS_SYNDROME=1 \
    -DHQC_RS_FAST_NON_CT="$HQC_RS_FAST_NON_CT" \
    -DHQC_GF_LUT_MUL="$HQC_GF_LUT_MUL" \
    -DHQC_RM_EXPAND_LUT="$HQC_RM_EXPAND_LUT" \
    -DHQC_RM_FUSED_FAST="$HQC_RM_FUSED_FAST" \
    -DHQC_RS_ROOTS_HVX="$HQC_RS_ROOTS_HVX" \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    -I "$SHARED_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/$PARAM_DIR" \
    "$GEN_DIR/hqc_skel.c" \
    "$SCRIPT_DIR/dsp/hqc_dsp.c" \
    "$SHARED_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" \
    "${common_sources[@]}" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -o "$BUILD_DIR/libhqc_skel.so"

echo "=== Building ARM64 host ==="
"$AARCH64_CC" -std=c11 -O2 -Wall -Wextra \
    -DHQC_PARAM_LEVEL="$HQC_PARAM_LEVEL" \
    "$DEFAULT_ITERS_DEFINE" \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    "$SCRIPT_DIR/host/main.c" \
    "$GEN_DIR/hqc_stub.c" \
    -L "$FASTRPC_LIB_DIR" \
    $AARCH64_LDFLAGS \
    -lcdsprpc -lpthread \
    -o "$BUILD_DIR/hqc_host"

if [ -f "$TESTSIG" ]; then
    cp "$TESTSIG" "$BUILD_DIR/"
fi

file "$BUILD_DIR/hqc_host"
file "$BUILD_DIR/libhqc_skel.so"
echo "  -> $BUILD_DIR/hqc_host"
echo "  -> $BUILD_DIR/libhqc_skel.so"
