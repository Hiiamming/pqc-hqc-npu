#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"

# The single source of truth for the deterministic fixture corpus is
# labs/scalar. Its gen script writes the fixture .c into
# $REPO_ROOT/shared/fixtures/, which every lab reads via $SHARED_DIR.
HQC_PARAM_LEVEL="${HQC_PARAM_LEVEL:-128}" \
    bash "$REPO_ROOT/labs/scalar/scripts/gen_decode_fixture.sh"
