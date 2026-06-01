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
    scalar|fastest)
        exec bash "$ROOT_DIR/scripts/lib/sim_decode_common.sh" \
            --variant "$variant" \
            --level "$level" \
            --bench "$bench" \
            --iters "$iters" \
            --stage "$stage" \
            --substage "$substage"
        ;;
    ct) lab="$ROOT_DIR/labs/ct" ;;
    *) echo "ERROR: variant must be scalar, fastest, or ct" >&2; exit 1 ;;
esac

case "$level" in
    128|192|256) ;;
    *) echo "ERROR: level must be 128, 192, or 256" >&2; exit 1 ;;
esac

case "$level" in
    128) hqc_id="1" ;;
    192) hqc_id="3" ;;
    256) hqc_id="5" ;;
esac
prefix="HQC${hqc_id}"
case "$bench" in
    decode)
        script="$lab/scripts/run_hqc${hqc_id}_decode_bench_hexagon.sh"
        env "${prefix}_BENCH_ITERS=$iters" bash "$script"
        ;;
    stage)
        script="$lab/scripts/run_hqc${hqc_id}_decode_stage_bench_hexagon.sh"
        if [ ! -f "$script" ]; then
            echo "ERROR: stage bench is not available for $variant HQC-$level" >&2
            exit 1
        fi
        env "${prefix}_BENCH_ITERS=$iters" "${prefix}_STAGE=$stage" bash "$script"
        ;;
    substage)
        script="$lab/scripts/run_hqc${hqc_id}_decode_substage_bench_hexagon.sh"
        if [ ! -f "$script" ]; then
            echo "ERROR: substage bench is not available for $variant HQC-$level" >&2
            exit 1
        fi
        env "${prefix}_BENCH_ITERS=$iters" "${prefix}_SUBSTAGE=$substage" bash "$script"
        ;;
    *)
        echo "ERROR: bench must be decode, stage, or substage" >&2
        exit 1
        ;;
esac
