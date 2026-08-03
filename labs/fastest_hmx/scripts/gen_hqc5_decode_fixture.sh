#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"

# Delegates to labs/scalar which is the single source of truth
# for the fixture corpus. The scalar gen script writes directly
# to $REPO_ROOT/shared/fixtures/, which ct/fastest read via $SHARED_DIR.
bash "$REPO_ROOT/labs/scalar/scripts/gen_hqc5_decode_fixture.sh"
