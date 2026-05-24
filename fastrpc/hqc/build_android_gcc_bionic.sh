#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
HEXAGON_SDK_ROOT="${HEXAGON_SDK_ROOT:-$ROOT_DIR/../tools/hexagon-sdk}"

AARCH64_GCC="${AARCH64_GCC:-aarch64-linux-gnu-gcc}"
ADB="${ADB:-adb}"
ANDROID_LIB_DIR="${ANDROID_LIB_DIR:-$SCRIPT_DIR/build/android-bionic-lib64}"
BUILD_DIR="$SCRIPT_DIR/build"
GEN_DIR="$SCRIPT_DIR/generated"

if ! command -v "$AARCH64_GCC" >/dev/null 2>&1 && [ ! -x "$AARCH64_GCC" ]; then
    echo "ERROR: $AARCH64_GCC not found" >&2
    exit 1
fi
if ! command -v readelf >/dev/null 2>&1; then
    echo "ERROR: readelf not found" >&2
    exit 1
fi
if ! command -v "$ADB" >/dev/null 2>&1 && [ ! -x "$ADB" ]; then
    echo "ERROR: $ADB not found" >&2
    exit 1
fi

mkdir -p "$ANDROID_LIB_DIR"

pull_lib_if_missing() {
    local device_path="$1"
    local base
    base="$(basename "$device_path")"
    if [ ! -f "$ANDROID_LIB_DIR/$base" ]; then
        "$ADB" pull "$device_path" "$ANDROID_LIB_DIR/"
    fi
}

pull_lib_if_missing /vendor/lib64/libcdsprpc.so
pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libc.so
pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libdl.so
pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libm.so
pull_lib_if_missing /apex/com.android.runtime/lib64/ld-android.so
pull_lib_if_missing /system/lib64/liblog.so

# Build the DSP skel and generated FastRPC files through the normal path. This
# also builds a Linux/glibc host, which is intentionally overwritten below.
HEXAGON_ARCH="${HEXAGON_ARCH:-v68}" \
HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}" \
bash "$SCRIPT_DIR/build.sh"

cat > "$BUILD_DIR/android_start.c" <<'EOF_START'
#include <stdint.h>

extern int main(int argc, char **argv);
extern void exit(int status);

__asm__(
    ".global _start\n"
    ".type _start, %function\n"
    "_start:\n"
    "    mov x0, sp\n"
    "    bl android_start_c\n"
);

__attribute__((noreturn)) void android_start_c(uintptr_t *args)
{
    int argc = (int)args[0];
    char **argv = (char **)&args[1];
    exit(main((int)argc, argv));
    __builtin_unreachable();
}
EOF_START

"$AARCH64_GCC" -std=c11 -O2 -Wall -Wextra -fPIC \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -DHQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}" \
    -DHQC_DEFAULT_BENCH_ITERS="${HQC_DEFAULT_BENCH_ITERS:-1000}" \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    -c "$BUILD_DIR/android_start.c" \
    -o "$BUILD_DIR/android_start.o"

"$AARCH64_GCC" -std=c11 -O2 -Wall -Wextra -fPIC \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -DHQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}" \
    -DHQC_DEFAULT_BENCH_ITERS="${HQC_DEFAULT_BENCH_ITERS:-1000}" \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    -c "$SCRIPT_DIR/host/main.c" \
    -o "$BUILD_DIR/host_main.android.o"

"$AARCH64_GCC" -std=c11 -O2 -Wall -Wextra -fPIC \
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0 \
    -DHQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}" \
    -I "$GEN_DIR" \
    -I "$HEXAGON_SDK_ROOT/incs" \
    -I "$HEXAGON_SDK_ROOT/incs/stddef" \
    -c "$GEN_DIR/hqc_stub.c" \
    -o "$BUILD_DIR/hqc_stub.android.o"

"$AARCH64_GCC" -nostdlib -pie \
    -Wl,--allow-shlib-undefined \
    -Wl,--dynamic-linker=/system/bin/linker64 \
    -Wl,-rpath,/apex/com.android.runtime/lib64/bionic \
    -Wl,-rpath,/system/lib64 \
    -Wl,-rpath,/vendor/lib64 \
    -Wl,-rpath-link,"$ANDROID_LIB_DIR" \
    -L "$ANDROID_LIB_DIR" \
    "$BUILD_DIR/android_start.o" \
    "$BUILD_DIR/host_main.android.o" \
    "$BUILD_DIR/hqc_stub.android.o" \
    -lcdsprpc -lc -ldl -lm -llog \
    -o "$BUILD_DIR/hqc_host"

interp="$(readelf -l "$BUILD_DIR/hqc_host" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')"
if [ "$interp" != "/system/bin/linker64" ]; then
    echo "ERROR: hqc_host interpreter is '$interp', expected /system/bin/linker64." >&2
    exit 1
fi

file "$BUILD_DIR/hqc_host"
file "$BUILD_DIR/libhqc_skel.so"
echo "Android GCC/Bionic workaround build complete:"
echo "  $BUILD_DIR/hqc_host"
echo "  $BUILD_DIR/libhqc_skel.so"
