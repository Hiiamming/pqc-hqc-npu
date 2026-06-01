#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/fastrpc_overhead}"
REMOTE_ROOT="${REMOTE_ROOT:-/data/local/tmp/QDC_files/hqc_fastrpc_overhead}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
LEVELS="${LEVELS:-128 192 256}"
REPEATS="${REPEATS:-5}"
PING_CALLS="${PING_CALLS:-10000}"
OPEN_CLOSE_CALLS="${OPEN_CLOSE_CALLS:-100}"
PAYLOAD_CALLS="${PAYLOAD_CALLS:-10000}"
DECODE_ONE_CALLS="${DECODE_ONE_CALLS:-10000}"
BENCH_ITERS="${BENCH_ITERS:-125}"
SKIP_BUILD="${SKIP_BUILD:-0}"

source "$ROOT_DIR/scripts/lib/android_device_common.sh"
android_device_require_adb

mkdir -p "$OUT_ROOT"

codeword_payload_bytes_for() {
    case "$1" in
        128) printf '2304\n' ;;
        192) printf '4480\n' ;;
        256) printf '7296\n' ;;
        *) echo "ERROR: unknown HQC level '$1'" >&2; exit 1 ;;
    esac
}

message_payload_bytes_for() {
    case "$1" in
        128|192|256) printf '128\n' ;;
        *) echo "ERROR: unknown HQC level '$1'" >&2; exit 1 ;;
    esac
}

for level in $LEVELS; do
    label="hqc${level}"
    out_dir="$OUT_ROOT/$(date +%Y%m%d_%H%M%S)_${label}"
    device_dir="$REMOTE_ROOT/$label"
    in_bytes="$(codeword_payload_bytes_for "$level")"
    out_bytes="$(message_payload_bytes_for "$level")"
    mkdir -p "$out_dir"

    if [ "$SKIP_BUILD" != "1" ]; then
        android_device_build_fastest "$level" "$BENCH_ITERS"
    fi

    android_device_deploy_fastrpc "$device_dir" >/dev/null

    {
        echo "level=$level"
        echo "repeats=$REPEATS"
        echo "ping_calls=$PING_CALLS"
        echo "open_close_calls=$OPEN_CLOSE_CALLS"
        echo "payload_calls=$PAYLOAD_CALLS"
        echo "payload_in_bytes=$in_bytes"
        echo "payload_out_bytes=$out_bytes"
        echo "decode_one_calls=$DECODE_ONE_CALLS"
        echo "bench_iters=$BENCH_ITERS"
        echo "device_dir=$device_dir"
    } > "$out_dir/run.info"

    for repeat in $(seq 1 "$REPEATS"); do
        repeat_dir="$out_dir/run${repeat}"
        mkdir -p "$repeat_dir"

        echo "=== $label run $repeat/$REPEATS open-close: $OPEN_CLOSE_CALLS calls ==="
        android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/open_close.log" open-close "$OPEN_CLOSE_CALLS"

        echo "=== $label run $repeat/$REPEATS ping: $PING_CALLS calls ==="
        android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/ping.log" ping "$PING_CALLS"

        for alloc in malloc rpcmem-cached rpcmem-uncached; do
            echo "=== $label run $repeat/$REPEATS payload-in $alloc: $PAYLOAD_CALLS calls, $in_bytes bytes ==="
            android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/payload_in_${alloc}.log" payload-in "$alloc" "$in_bytes" "$PAYLOAD_CALLS"

            echo "=== $label run $repeat/$REPEATS payload-out $alloc: $PAYLOAD_CALLS calls, $out_bytes bytes ==="
            android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/payload_out_${alloc}.log" payload-out "$alloc" "$out_bytes" "$PAYLOAD_CALLS"

            echo "=== $label run $repeat/$REPEATS payload-inout $alloc: $PAYLOAD_CALLS calls, $in_bytes->$out_bytes bytes ==="
            android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/payload_inout_${alloc}.log" payload-inout "$alloc" "$in_bytes" "$out_bytes" "$PAYLOAD_CALLS"
        done

        echo "=== $label run $repeat/$REPEATS decode-one: $DECODE_ONE_CALLS calls ==="
        android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/decode_one.log" decode-one "$DECODE_ONE_CALLS"

        echo "=== $label run $repeat/$REPEATS bench: $BENCH_ITERS iters ==="
        android_device_run_fastrpc_remote "$device_dir" "$repeat_dir/bench.log" bench "$BENCH_ITERS"
    done

    echo "Wrote $out_dir"
done
