#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT="$(cd "$PROJECT_DIR/../.." && pwd)"
SHARED_DIR="$REPO_ROOT/shared"

echo "ERROR: $PROJECT_DIR is Hexagon-only CT intrinsic now; use labs/scalar for host/scalar checks."
exit 1
