#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
level="${LEVEL:-128}"
target="${TARGET:-linux}"
iters="${ITERS:-10000}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --level) level="$2"; shift 2 ;;
        --target) target="$2"; shift 2 ;;
        --iters) iters="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/build_cpu_decode.sh [--level 128|192|256] [--target linux|android] [--iters N]" >&2
            exit 0
            ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

case "$target" in
    linux) builder="$ROOT_DIR/runners/scalar_cpu/build_arm64.sh" ;;
    android) builder="$ROOT_DIR/runners/scalar_cpu/build_android_gcc_bionic.sh" ;;
    *) echo "ERROR: target must be linux or android" >&2; exit 1 ;;
esac

env HQC_PARAM_LEVEL="$level" HQC_BENCH_ITERS="$iters" bash "$builder"
