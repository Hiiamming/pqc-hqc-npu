#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/fastrpc_overhead}"
REMOTE_ROOT="${REMOTE_ROOT:-/data/local/tmp/QDC_files/hqc_fastrpc_overhead}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v68}"
LEVELS="${LEVELS:-128}"
REPEATS="${REPEATS:-3}"
PING_CALLS="${PING_CALLS:-100000}"
OPEN_CLOSE_CALLS="${OPEN_CLOSE_CALLS:-1000}"
PAYLOAD_CALLS="${PAYLOAD_CALLS:-100000}"
DECODE_ONE_CALLS="${DECODE_ONE_CALLS:-160000}"
BENCH_ITERS="${BENCH_ITERS:-10000}"
SKIP_BUILD="${SKIP_BUILD:-0}"

if [ ! -x "$ADB" ] && ! command -v "$ADB" >/dev/null 2>&1; then
    echo "ERROR: ADB executable not found: $ADB" >&2
    exit 1
fi

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

run_remote() {
    local device_dir="$1"
    local log="$2"
    shift 2

    "$ADB" shell \
        "cd '$device_dir' && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host $*" \
        | tee "$log"
}

for level in $LEVELS; do
    label="hqc${level}"
    out_dir="$OUT_ROOT/$(date +%Y%m%d_%H%M%S)_${label}"
    device_dir="$REMOTE_ROOT/$label"
    in_bytes="$(codeword_payload_bytes_for "$level")"
    out_bytes="$(message_payload_bytes_for "$level")"
    mkdir -p "$out_dir"

    if [ "$SKIP_BUILD" != "1" ]; then
        ADB="$ADB" \
        HEXAGON_ARCH="$HEXAGON_ARCH" \
        HQC_PARAM_LEVEL="$level" \
        HQC_DEFAULT_BENCH_ITERS="$BENCH_ITERS" \
        HQC_PROJECT_DIR="$ROOT_DIR/labs/fastest" \
            bash "$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh"
    fi

    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi

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
        run_remote "$device_dir" "$repeat_dir/open_close.log" open-close "$OPEN_CLOSE_CALLS"

        echo "=== $label run $repeat/$REPEATS ping: $PING_CALLS calls ==="
        run_remote "$device_dir" "$repeat_dir/ping.log" ping "$PING_CALLS"

        for alloc in malloc rpcmem-cached rpcmem-uncached; do
            echo "=== $label run $repeat/$REPEATS payload-in $alloc: $PAYLOAD_CALLS calls, $in_bytes bytes ==="
            run_remote "$device_dir" "$repeat_dir/payload_in_${alloc}.log" payload-in "$alloc" "$in_bytes" "$PAYLOAD_CALLS"

            echo "=== $label run $repeat/$REPEATS payload-out $alloc: $PAYLOAD_CALLS calls, $out_bytes bytes ==="
            run_remote "$device_dir" "$repeat_dir/payload_out_${alloc}.log" payload-out "$alloc" "$out_bytes" "$PAYLOAD_CALLS"

            echo "=== $label run $repeat/$REPEATS payload-inout $alloc: $PAYLOAD_CALLS calls, $in_bytes->$out_bytes bytes ==="
            run_remote "$device_dir" "$repeat_dir/payload_inout_${alloc}.log" payload-inout "$alloc" "$in_bytes" "$out_bytes" "$PAYLOAD_CALLS"
        done

        echo "=== $label run $repeat/$REPEATS decode-one: $DECODE_ONE_CALLS calls ==="
        run_remote "$device_dir" "$repeat_dir/decode_one.log" decode-one "$DECODE_ONE_CALLS"

        echo "=== $label run $repeat/$REPEATS bench: $BENCH_ITERS iters ==="
        run_remote "$device_dir" "$repeat_dir/bench.log" bench "$BENCH_ITERS"
    done

    echo "Wrote $out_dir"
done
