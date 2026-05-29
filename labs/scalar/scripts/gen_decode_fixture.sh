#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"

HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}"
case "$HQC_PARAM_LEVEL" in
    128) PARAM_SET=1 ;;
    192) PARAM_SET=3 ;;
    256) PARAM_SET=5 ;;
    *) echo "ERROR: HQC_PARAM_LEVEL must be 128, 192, or 256" >&2; exit 1 ;;
esac

OUT="$PROJECT_DIR/build/gen_hqc${PARAM_SET}_decode_fixture"
FIXTURE="$SHARED_DIR/fixtures/hqc${PARAM_SET}_decode_fixture.c"
mkdir -p "$(dirname "$OUT")" "$(dirname "$FIXTURE")"

gcc -std=c11 -O2 -Wall -Wextra \
    -I "$SHARED_DIR/src/common" \
    -I "$PROJECT_DIR/src/common" \
    -I "$SHARED_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref" \
    -I "$PROJECT_DIR/src/ref/hqc-${PARAM_SET}" \
    "$SHARED_DIR/tools/gen_hqc${PARAM_SET}_decode_fixture.c" \
    "$SHARED_DIR/src/common/code.c" \
    "$PROJECT_DIR/src/common/fft.c" \
    "$PROJECT_DIR/src/ref/gf.c" \
    "$PROJECT_DIR/src/ref/reed_muller.c" \
    "$PROJECT_DIR/src/ref/reed_solomon.c" \
    -o "$OUT"

"$OUT" > "$FIXTURE"
echo "Generated $FIXTURE"
