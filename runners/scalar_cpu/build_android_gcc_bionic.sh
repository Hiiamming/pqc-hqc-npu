#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
PROJECT_DIR="$ROOT_DIR/labs/scalar"

AARCH64_GCC="${AARCH64_GCC:-aarch64-linux-gnu-gcc}"
ADB="${ADB:-adb}"
ANDROID_LIB_DIR="${ANDROID_LIB_DIR:-$SCRIPT_DIR/build/android-bionic-lib64}"
BUILD_DIR="$SCRIPT_DIR/build"

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

if [ ! -f "$PROJECT_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" ]; then
    "$PROJECT_DIR/scripts/gen_${FIXTURE_PREFIX}_decode_fixture.sh"
fi

mkdir -p "$BUILD_DIR" "$ANDROID_LIB_DIR"

pull_lib_if_missing() {
    local device_path="$1"
    local base
    base="$(basename "$device_path")"
    if [ ! -f "$ANDROID_LIB_DIR/$base" ]; then
        "$ADB" pull "$device_path" "$ANDROID_LIB_DIR/"
    fi
}

pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libc.so
pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libdl.so
pull_lib_if_missing /apex/com.android.runtime/lib64/bionic/libm.so
pull_lib_if_missing /apex/com.android.runtime/lib64/ld-android.so

cat > "$BUILD_DIR/android_start.cpu.c" <<'EOF_START'
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
    exit(main(argc, argv));
    __builtin_unreachable();
}
EOF_START

common_cflags=(
    -std=c11 -O2 -Wall -Wextra -fPIC
    -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=0
    -DHQC_PARAM_LEVEL="$HQC_PARAM_LEVEL"
    -DHQC${HQC_PARAM_LEVEL}_BENCH_ITERS="$BENCH_ITERS"
    -I "$PROJECT_DIR/fixtures"
    -I "$PROJECT_DIR/src/common"
    -I "$PROJECT_DIR/src/ref"
    -I "$PROJECT_DIR/src/ref/$PARAM_DIR"
)

echo "=== Building HQC-$HQC_PARAM_LEVEL scalar decode baseline for Android/Bionic, iters=$BENCH_ITERS ==="
"$AARCH64_GCC" "${common_cflags[@]}" -c "$BUILD_DIR/android_start.cpu.c" -o "$BUILD_DIR/android_start.cpu.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$SCRIPT_DIR/hqc_decode_bench_arm64.c" -o "$BUILD_DIR/hqc${HQC_PARAM_LEVEL}_decode_bench_arm64.android.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$PROJECT_DIR/fixtures/${FIXTURE_PREFIX}_decode_fixture.c" -o "$BUILD_DIR/${FIXTURE_PREFIX}_decode_fixture.android.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$PROJECT_DIR/src/common/fft.c" -o "$BUILD_DIR/fft.android.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$PROJECT_DIR/src/ref/gf.c" -o "$BUILD_DIR/gf.android.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$PROJECT_DIR/src/ref/reed_muller.c" -o "$BUILD_DIR/reed_muller.android.o"
"$AARCH64_GCC" "${common_cflags[@]}" -c "$PROJECT_DIR/src/ref/reed_solomon.c" -o "$BUILD_DIR/reed_solomon.android.o"

out="$BUILD_DIR/hqc${HQC_PARAM_LEVEL}_decode_bench_arm64_android"
"$AARCH64_GCC" -nostdlib -pie \
    -Wl,--dynamic-linker=/system/bin/linker64 \
    -Wl,-rpath,/apex/com.android.runtime/lib64/bionic \
    -Wl,-rpath-link,"$ANDROID_LIB_DIR" \
    -L "$ANDROID_LIB_DIR" \
    "$BUILD_DIR/android_start.cpu.o" \
    "$BUILD_DIR/hqc${HQC_PARAM_LEVEL}_decode_bench_arm64.android.o" \
    "$BUILD_DIR/${FIXTURE_PREFIX}_decode_fixture.android.o" \
    "$BUILD_DIR/fft.android.o" \
    "$BUILD_DIR/gf.android.o" \
    "$BUILD_DIR/reed_muller.android.o" \
    "$BUILD_DIR/reed_solomon.android.o" \
    -lc -ldl -lm \
    -o "$out"

interp="$(readelf -l "$out" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')"
if [ "$interp" != "/system/bin/linker64" ]; then
    echo "ERROR: $out interpreter is '$interp', expected /system/bin/linker64." >&2
    exit 1
fi

file "$out"
echo "  -> $out"
