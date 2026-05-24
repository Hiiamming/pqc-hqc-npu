#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
backend="${BACKEND:-cpu}"
level="${LEVEL:-128}"
iters="${ITERS:-10000}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --backend) backend="$2"; shift 2 ;;
        --level) level="$2"; shift 2 ;;
        --iters) iters="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/measure_linux_decode.sh [--backend cpu|fastest|ct] [--level 128|192|256] [--iters N]" >&2
            exit 0
            ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

case "$backend" in
    cpu)
        "$ROOT_DIR/scripts/build_cpu_decode.sh" --target linux --level "$level" --iters "$iters"
        exec "$ROOT_DIR/runners/scalar_cpu/build/hqc${level}_decode_bench_arm64"
        ;;
    fastest|ct)
        "$ROOT_DIR/scripts/build_fastrpc_decode.sh" --target linux --variant "$backend" --level "$level" --iters "$iters"
        export LD_LIBRARY_PATH="$ROOT_DIR/fastrpc/hqc/build:${LD_LIBRARY_PATH:-}"
        exec "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$iters"
        ;;
    *) echo "ERROR: backend must be cpu, fastest, or ct" >&2; exit 1 ;;
esac
