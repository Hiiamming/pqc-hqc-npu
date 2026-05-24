#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

"$ROOT_DIR/scripts/measure_android_decode.sh"
"$ROOT_DIR/scripts/measure_android_ct_npu.sh"
