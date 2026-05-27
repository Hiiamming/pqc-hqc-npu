#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
PROJECT_PARENT="$(cd "$PROJECT_DIR/.." && pwd)"
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

if [ ! -f "$PROJECT_DIR/fixtures/hqc3_decode_fixture.c" ]; then
    "$SCRIPT_DIR/gen_hqc3_decode_fixture.sh"
fi

OUT="$PROJECT_DIR/build/hqc3_decode_bench_hexagon"
BENCH_ITERS="${HQC3_BENCH_ITERS:-100}"
mkdir -p "$(dirname "$OUT")"

echo "=== Compiling HQC-192 decode benchmark for Hexagon fastest HVX/HMX path, iters=$BENCH_ITERS ==="
"$CLANG" -O2 -mv75 \
    -ffunction-sections -fdata-sections \
    -mhvx -mhvx-length=128B \
    -mhmx \
    -DARCHV=75 \
    -DHQC3_BENCH_ITERS="$BENCH_ITERS" \
    -I "$PROJECT_DIR/fixtures" \
    -I "$PROJECT_DIR/src/common" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-3" \
    -I "$H2_INSTALL/include" \
    -I "$H2_KERNEL" \
    -moslib=h2 \
    -Wl,-L,"$H2_INSTALL/lib" \
    -Wl,--section-start=.start=0x02000000 \
    -Wl,--gc-sections \
    -o "$OUT" \
    "$PROJECT_DIR/demos/hqc3_decode_bench.c" \
    "$PROJECT_DIR/fixtures/hqc3_decode_fixture.c" \
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
