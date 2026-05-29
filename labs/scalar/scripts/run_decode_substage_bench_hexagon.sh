#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_PARENT="$(cd "$PROJECT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"
if [ -n "${HEXAGON_TUTORIAL_ROOT:-}" ]; then
    TUTORIAL_ROOT="$HEXAGON_TUTORIAL_ROOT"
elif [ -d "$PROJECT_PARENT/tools/hexagon-sdk" ]; then
    TUTORIAL_ROOT="$PROJECT_PARENT"
else
    TUTORIAL_ROOT="$(cd "$PROJECT_PARENT/.." && pwd)"
fi
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

SUBSTAGE="${HQC_SUBSTAGE:-4}"
BENCH_ITERS="${HQC_BENCH_ITERS:-10}"
OUT="$PROJECT_DIR/build/hqc${PARAM_SET}_decode_substage_bench_hexagon_stage${SUBSTAGE}"
mkdir -p "$(dirname "$OUT")"

echo "=== Compiling HQC-$HQC_PARAM_LEVEL substage benchmark for Hexagon scalar baseline path, substage=$SUBSTAGE, iters=$BENCH_ITERS ==="
"$CLANG" -O2 -mv75 \
    -ffunction-sections -fdata-sections \
    -mhvx -mhvx-length=128B \
    -mhmx \
    -DARCHV=75 \
    -DHQC_ENABLE_SUBSTAGE_BENCH=1 \
    -DHQC_SUBSTAGE="$SUBSTAGE" \
    -DHQC_BENCH_ITERS="$BENCH_ITERS" \
    -I "$SHARED_DIR/fixtures" \
    -I "$SHARED_DIR/src/common" \
    -I "$PROJECT_DIR/src/common" \
    -I "$SHARED_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-${PARAM_SET}" \
    -I "$H2_INSTALL/include" \
    -I "$H2_KERNEL" \
    -moslib=h2 \
    -Wl,-L,"$H2_INSTALL/lib" \
    -Wl,--section-start=.start=0x02000000 \
    -Wl,--gc-sections \
    -o "$OUT" \
    "$PROJECT_DIR/demos/hqc${PARAM_SET}_decode_substage_bench.c" \
    "$SHARED_DIR/fixtures/hqc${PARAM_SET}_decode_fixture.c" \
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
