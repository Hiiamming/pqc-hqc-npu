#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

echo "ERROR: $PROJECT_DIR is Hexagon-only fastest HVX/HMX now; use labs/scalar for host/scalar checks."
exit 1
