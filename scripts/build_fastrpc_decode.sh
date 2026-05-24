#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
variant="${VARIANT:-fastest}"
level="${LEVEL:-128}"
target="${TARGET:-linux}"
iters="${ITERS:-10000}"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --variant) variant="$2"; shift 2 ;;
        --level) level="$2"; shift 2 ;;
        --target) target="$2"; shift 2 ;;
        --iters) iters="$2"; shift 2 ;;
        -h|--help)
            echo "Usage: scripts/build_fastrpc_decode.sh [--variant fastest|ct] [--level 128|192|256] [--target linux|android] [--iters N]" >&2
            exit 0
            ;;
        *) echo "ERROR: unknown argument: $1" >&2; exit 1 ;;
    esac
done

case "$variant" in
    fastest)
        project_dir="$ROOT_DIR/labs/fastest"
        extra_env=(
            HQC_RS_FAST_NON_CT=1
            HQC_GF_LUT_MUL=1
            HQC_RM_EXPAND_LUT=1
            HQC_RM_FUSED_FAST=1
            HQC_RS_ROOTS_HVX=1
        )
        ;;
    ct)
        project_dir="$ROOT_DIR/labs/ct"
        extra_env=(HQC_RS_ROOTS_HVX=1)
        ;;
    *) echo "ERROR: variant must be fastest or ct" >&2; exit 1 ;;
esac

case "$target" in
    linux) builder="$ROOT_DIR/fastrpc/hqc/build.sh" ;;
    android) builder="$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh" ;;
    *) echo "ERROR: target must be linux or android" >&2; exit 1 ;;
esac

env \
    HQC_PROJECT_DIR="$project_dir" \
    HQC_PARAM_LEVEL="$level" \
    HQC_DEFAULT_BENCH_ITERS="$iters" \
    "${extra_env[@]}" \
    bash "$builder"
