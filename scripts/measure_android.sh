#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"

suite="${SUITE:-paper}"
levels="${LEVELS:-128 192 256}"
repeats="${REPEATS:-}"
target_decodes="${TARGET_DECODES:-}"
out_root="${OUT_ROOT:-}"
result_md="${RESULT_MD:-}"
skip_build="${SKIP_BUILD:-0}"
run_sanity="${RUN_SANITY:-0}"
sanity_only="${SANITY_ONLY:-0}"

usage() {
    cat >&2 <<'EOF'
Usage: scripts/measure_android.sh [options]

Android real-device benchmark entrypoint for the CPU scalar baseline and the
FastRPC cDSP fastest path.

Options:
  --suite paper|boundary  paper: latency, direct energy, and process CPU
                          boundary: FastRPC open/ping/payload/decode overhead
  --levels "128 192 256" HQC parameter levels to measure
  --repeats N             measured repeats; defaults to 5
  --target-decodes N      decodes per paper-suite workload; defaults to 32000
  --out-root DIR          local raw-result directory
  --result-md FILE        markdown result log for the paper suite
  --skip-build            reuse already-deployed artifacts
  --sanity                run the optional historical-baseline sanity check
  --sanity-only           run only the optional historical-baseline sanity check
  -h, --help              show this help

Environment:
  ADB                     adb executable
  HEXAGON_ARCH            cDSP architecture; defaults to v73

Examples:
  ADB=/path/to/adb scripts/measure_android.sh --suite paper
  ADB=/path/to/adb scripts/measure_android.sh --suite boundary
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --suite) suite="$2"; shift 2 ;;
        --levels) levels="$2"; shift 2 ;;
        --repeats) repeats="$2"; shift 2 ;;
        --target-decodes) target_decodes="$2"; shift 2 ;;
        --out-root) out_root="$2"; shift 2 ;;
        --result-md) result_md="$2"; shift 2 ;;
        --skip-build) skip_build=1; shift ;;
        --sanity) run_sanity=1; shift ;;
        --sanity-only) run_sanity=1; sanity_only=1; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; usage; exit 2 ;;
    esac
done

export LEVELS="$levels"
export HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
export SKIP_BUILD="$skip_build"
[ -n "$out_root" ] && export OUT_ROOT="$out_root"

case "$suite" in
    paper)
        export REPEATS="${repeats:-5}"
        export CPU_TARGET_DECODES="${CPU_TARGET_DECODES:-${target_decodes:-32000}}"
        export NPU_TARGET_DECODES="${NPU_TARGET_DECODES:-${target_decodes:-32000}}"
        export RUN_SANITY="$run_sanity"
        export SANITY_ONLY="$sanity_only"
        [ -n "$result_md" ] && export RESULT_MD="$result_md"
        exec "$ROOT_DIR/scripts/lib/android_measure_paper.sh"
        ;;
    boundary)
        if [ "$run_sanity" = "1" ]; then
            echo "ERROR: --sanity and --sanity-only apply only to --suite paper" >&2
            exit 2
        fi
        export REPEATS="${repeats:-5}"
        export PING_CALLS="${PING_CALLS:-10000}"
        export OPEN_CLOSE_CALLS="${OPEN_CLOSE_CALLS:-100}"
        export PAYLOAD_CALLS="${PAYLOAD_CALLS:-10000}"
        export DECODE_ONE_CALLS="${DECODE_ONE_CALLS:-10000}"
        export BENCH_ITERS="${BENCH_ITERS:-125}"
        exec "$ROOT_DIR/scripts/lib/android_measure_boundary.sh"
        ;;
    *)
        echo "ERROR: --suite must be paper or boundary" >&2
        exit 2
        ;;
esac
