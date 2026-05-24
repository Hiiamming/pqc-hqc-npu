#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$ROOT_DIR/hqc_fastrpc_intrinsic/build"
DEVICE_DIR="${DEVICE_DIR:-/data/local/tmp/QDC_files/hqc_fastrpc_intrinsic}"
SMOKE_ITERS="${SMOKE_ITERS:-10}"
BENCH_ITERS="${BENCH_ITERS:-10000}"

if [ "${SKIP_BUILD:-0}" != "1" ]; then
    HQC_PARAM_LEVEL=128 HEXAGON_ARCH="${HEXAGON_ARCH:-v73}" \
        bash "$ROOT_DIR/hqc_fastrpc_intrinsic/build_android.sh"
fi

if command -v readelf >/dev/null 2>&1; then
    interp="$(readelf -l "$BUILD_DIR/hqc_host" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')"
    if [ "$interp" != "/system/bin/linker64" ]; then
        echo "ERROR: $BUILD_DIR/hqc_host uses '$interp', expected /system/bin/linker64." >&2
        exit 1
    fi
fi

adb shell "mkdir -p '$DEVICE_DIR'"
adb push "$BUILD_DIR/hqc_host" "$DEVICE_DIR/"
adb push "$BUILD_DIR/libhqc_skel.so" "$DEVICE_DIR/"
if [ -f "$BUILD_DIR/testsig-0xaa3ec42e.so" ]; then
    adb push "$BUILD_DIR/testsig-0xaa3ec42e.so" "$DEVICE_DIR/"
fi

adb shell "cd '$DEVICE_DIR' && chmod +x hqc_host && \
export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && \
export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && \
echo '=== smoke iters=$SMOKE_ITERS ===' && ./hqc_host '$SMOKE_ITERS' && \
echo '=== bench iters=$BENCH_ITERS ===' && ./hqc_host '$BENCH_ITERS'"
