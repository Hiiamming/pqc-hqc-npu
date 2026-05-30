#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"
REF_SRC="${HQC_REFERENCE_SRC:-$REPO_ROOT/git/hqc_gitlab/src}"
PARAM_DIR="hqc-3"

OUT="$PROJECT_DIR/build/gen_hqc3_decode_fixture"
FIXTURE="$SHARED_DIR/fixtures/hqc3_decode_fixture.c"
mkdir -p "$(dirname "$OUT")" "$(dirname "$FIXTURE")"

if [ ! -d "$REF_SRC" ]; then
    echo "ERROR: HQC reference source not found: $REF_SRC" >&2
    exit 1
fi

gcc -std=c11 -O2 -Wall -Wextra \
    -I "$REF_SRC/common" \
    -I "$REF_SRC/common/$PARAM_DIR" \
    -I "$REF_SRC/ref" \
    -I "$REF_SRC/ref/$PARAM_DIR" \
    "$SHARED_DIR/tools/gen_hqc3_decode_fixture.c" \
    "$REF_SRC/common/code.c" \
    "$REF_SRC/common/crypto_memset.c" \
    "$REF_SRC/common/fft.c" \
    "$REF_SRC/ref/gf.c" \
    "$REF_SRC/ref/reed_muller.c" \
    "$REF_SRC/ref/reed_solomon.c" \
    -o "$OUT"

"$OUT" > "$FIXTURE"
echo "Generated $FIXTURE"
