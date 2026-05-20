#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_DIR="$ROOT_DIR/hqc_lab_scalar"
HEXAGON_SDK_ROOT="${HEXAGON_SDK_ROOT:-$ROOT_DIR/../tools/hexagon-sdk}"

QAIC="${QAIC:-$HEXAGON_SDK_ROOT/ipc/fastrpc/qaic/Ubuntu/qaic}"
HEXAGON_CLANG="${HEXAGON_CLANG:-$HEXAGON_SDK_ROOT/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-clang}"
AARCH64_CC="${AARCH64_CC:-aarch64-linux-gnu-gcc}"
FASTRPC_LIB_DIR="${FASTRPC_LIB_DIR:-$HEXAGON_SDK_ROOT/ipc/fastrpc/remote/ship/UbuntuARM_aarch64}"
AARCH64_LDFLAGS="${AARCH64_LDFLAGS:-}"
TESTSIG="$HEXAGON_SDK_ROOT/tools/elfsigner/output/testsig-0xaa3ec42e.so"

BUILD_DIR="$SCRIPT_DIR/build"
GEN_DIR="$SCRIPT_DIR/generated"

for tool in "$QAIC" "$HEXAGON_CLANG" "$AARCH64_CC"; do
    if ! command -v "$tool" >/dev/null 2>&1 && [ ! -x "$tool" ]; then
        echo "ERROR: $tool not found" >&2
        exit 1
    fi
done

if [ ! -f "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" ]; then
    "$PROJECT_DIR/scripts/gen_hqc128_decode_fixture.sh"
fi

mkdir -p "$BUILD_DIR" "$GEN_DIR"

echo "=== Generating FastRPC stub/skel ==="
(
    cd "$GEN_DIR"
    "$QAIC" -I "$HEXAGON_SDK_ROOT/incs" "$SCRIPT_DIR/hqc.idl"
)

echo "=== Building cDSP scalar skel ==="
"$HEXAGON_CLANG" -O2 -fPIC -shared \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-1" \
    "$GEN_DIR/hqc_skel.c" \
    "$SCRIPT_DIR/dsp/hqc_dsp.c" \
    "$PROJECT_DIR/fixtures/hqc128_decode_fixture.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -o "$BUILD_DIR/libhqc_skel.so"

echo "=== Building ARM64 host ==="
"$AARCH64_CC" -std=c11 -O2 -Wall -Wextra \
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
