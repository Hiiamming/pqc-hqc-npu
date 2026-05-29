#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

variant="${VARIANT:-fastest}"
level="${LEVEL:-128}"
bench="${BENCH:-decode}"
iters="${ITERS:-10}"
stage="${STAGE:-0}"
substage="${SUBSTAGE:-4}"

usage() {
    cat >&2 <<'EOF'
Usage: scripts/sim_decode.sh [--variant scalar|fastest|ct] [--level 128|192|256]
                             [--bench decode|stage|substage] [--iters N]
                             [--stage N] [--substage N]
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --variant) variant="$2"; shift 2 ;;
        --level) level="$2"; shift 2 ;;
        --bench) bench="$2"; shift 2 ;;
        --iters) iters="$2"; shift 2 ;;
        --stage) stage="$2"; shift 2 ;;
        --substage) substage="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

case "$variant" in
    scalar) lab="$ROOT_DIR/labs/scalar" ;;
    fastest) lab="$ROOT_DIR/labs/fastest" ;;
    ct) lab="$ROOT_DIR/labs/ct" ;;
    *) echo "ERROR: variant must be scalar, fastest, or ct" >&2; exit 1 ;;
esac

case "$level" in
    128|192|256) ;;
    *) echo "ERROR: level must be 128, 192, or 256" >&2; exit 1 ;;
esac

# After the script refactor, every lab exposes a single parametric script
# per benchmark family. The level is selected via HQC_PARAM_LEVEL and the
# iteration count via HQC_BENCH_ITERS. Stage / substage indices use the
# same generic env-var convention.
case "$bench" in
    decode)
        script="$lab/scripts/run_decode_bench_hexagon.sh"
        env_vars=("HQC_PARAM_LEVEL=$level" "HQC_BENCH_ITERS=$iters")
        ;;
    stage)
        script="$lab/scripts/run_decode_stage_bench_hexagon.sh"
        env_vars=("HQC_PARAM_LEVEL=$level" "HQC_BENCH_ITERS=$iters" "HQC_STAGE=$stage")
        ;;
    substage)
        script="$lab/scripts/run_decode_substage_bench_hexagon.sh"
        env_vars=("HQC_PARAM_LEVEL=$level" "HQC_BENCH_ITERS=$iters" "HQC_SUBSTAGE=$substage")
        ;;
    *)
        echo "ERROR: bench must be decode, stage, or substage" >&2
        exit 1
        ;;
esac

if [ ! -f "$script" ]; then
    echo "ERROR: $bench bench is not available for variant '$variant'" >&2
    echo "       (missing: $script)" >&2
    exit 1
fi

env "${env_vars[@]}" bash "$script"
