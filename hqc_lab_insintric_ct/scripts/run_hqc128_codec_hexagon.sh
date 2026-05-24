#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/.." && pwd)"
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
MODE="${HQC_MODE:-128}"

case "$MODE" in
    128)
        PARAM_DIR="hqc-1"
        ;;
    192)
        PARAM_DIR="hqc-192"
        ;;
    256)
        PARAM_DIR="hqc-256"
        ;;
    *)
        echo "ERROR: HQC_MODE must be 128, 192, or 256."
        exit 1
        ;;
esac

for f in "$CLANG" "$SIM" "$BOOTER"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run install_tools.sh first or set HEXAGON_TUTORIAL_ROOT/HEXAGON_SDK_ROOT."
        exit 1
    fi
done

OUT="$PROJECT_DIR/build/hqc${MODE}_codec_demo_hexagon"
mkdir -p "$(dirname "$OUT")"

DEMO_SRC="$PROJECT_DIR/demos/hqc128_codec_demo.c"
if [ ! -f "$DEMO_SRC" ]; then
    DEMO_SRC="$REPO_ROOT/demos/hqc128_codec_demo.c"
fi

SRCS=(
    "$DEMO_SRC"
    "$PROJECT_DIR/src/common/code.c"
    "$PROJECT_DIR/src/ref/gf.c"
    "$PROJECT_DIR/src/ref/reed_muller.c"
    "$PROJECT_DIR/src/ref/reed_solomon.c"
)

echo "=== Compiling HQC-$MODE codec for Hexagon ==="
"$CLANG" -O2 -mv75 \
    -mhvx -mhvx-length=128B \
    -mhmx \
    -DARCHV=75 \
    -DHQC_MODE="$MODE" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/$PARAM_DIR" \
    -I "$H2_INSTALL/include" \
    -I "$H2_KERNEL" \
    -moslib=h2 \
    -Wl,-L,"$H2_INSTALL/lib" \
    -Wl,--section-start=.start=0x02000000 \
    -o "$OUT" "${SRCS[@]}"
echo "  -> $OUT"

echo
echo "=== Running on hexagon-sim ==="
"$SIM" --mv75 --mhmx 1 --simulated_returnval \
    -- "$BOOTER" \
    --ext_power 1 \
    --use_ext 1 \
    --fence_hi 0xfe000000 \
    "$OUT"
