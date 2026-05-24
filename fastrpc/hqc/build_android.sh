#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

ANDROID_API="${ANDROID_API:-34}"
ANDROID_HOST_TAG="${ANDROID_HOST_TAG:-linux-x86_64}"
ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-}"

if [ -z "$ANDROID_NDK_HOME" ]; then
    for ndk_root in \
        "${ANDROID_HOME:-}/ndk" \
        "${ANDROID_SDK_ROOT:-}/ndk" \
        "$HOME/Android/Sdk/ndk"; do
        if [ -d "$ndk_root" ]; then
            ANDROID_NDK_HOME="$(find "$ndk_root" -mindepth 1 -maxdepth 1 -type d | sort -V | tail -n 1)"
            break
        fi
    done
fi

if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ERROR: Android NDK not found. Set ANDROID_NDK_HOME=/path/to/android-ndk." >&2
    exit 1
fi

ANDROID_TOOLCHAIN="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$ANDROID_HOST_TAG/bin"
ANDROID_CC="$ANDROID_TOOLCHAIN/aarch64-linux-android${ANDROID_API}-clang"
if [ ! -x "$ANDROID_CC" ]; then
    echo "ERROR: $ANDROID_CC not found. Check ANDROID_NDK_HOME, ANDROID_API, and ANDROID_HOST_TAG." >&2
    exit 1
fi

ANDROID_FASTRPC_LIB_DIR="${ANDROID_FASTRPC_LIB_DIR:-$SCRIPT_DIR/build/android-lib64}"
mkdir -p "$ANDROID_FASTRPC_LIB_DIR"

if [ ! -f "$ANDROID_FASTRPC_LIB_DIR/libcdsprpc.so" ]; then
    if ! command -v adb >/dev/null 2>&1; then
        echo "ERROR: adb not found and $ANDROID_FASTRPC_LIB_DIR/libcdsprpc.so is missing." >&2
        exit 1
    fi
    adb pull /vendor/lib64/libcdsprpc.so "$ANDROID_FASTRPC_LIB_DIR/"
fi

export AARCH64_CC="${AARCH64_CC:-$ANDROID_CC}"
export FASTRPC_LIB_DIR="${FASTRPC_LIB_DIR:-$ANDROID_FASTRPC_LIB_DIR}"
export AARCH64_LDFLAGS="${AARCH64_LDFLAGS:--Wl,-rpath,/apex/com.android.runtime/lib64/bionic -Wl,-rpath,/system/lib64 -Wl,-rpath,/vendor/lib64}"
export HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"

bash "$SCRIPT_DIR/build.sh"

if command -v readelf >/dev/null 2>&1; then
    interp="$(readelf -l "$SCRIPT_DIR/build/hqc_host" | sed -n 's/.*Requesting program interpreter: \(.*\)]/\1/p')"
    if [ "$interp" != "/system/bin/linker64" ]; then
        echo "ERROR: hqc_host interpreter is '$interp', expected /system/bin/linker64." >&2
        exit 1
    fi
fi

echo "Android FastRPC intrinsic build complete:"
echo "  $SCRIPT_DIR/build/hqc_host"
echo "  $SCRIPT_DIR/build/libhqc_skel.so"
