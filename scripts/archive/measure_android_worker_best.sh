#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/qprof/qprof_hqc_worker_best_20260529}"
BASELINE_ROOT="${BASELINE_ROOT:-$ROOT_DIR/results/qprof/qprof_hqc_whole_rerun}"
ENERGY_REMOTE_DIR="${ENERGY_REMOTE_DIR:-/data/local/tmp/QDC_files/hqc_worker_measure}"
DIRECT_SAMPLE_INTERVAL="${DIRECT_SAMPLE_INTERVAL:-0.1}"
DIRECT_IDLE_SECONDS="${DIRECT_IDLE_SECONDS:-10}"
DIRECT_IDLE_POSITION="${DIRECT_IDLE_POSITION:-both}"
PROCESS_CPU_SAMPLE_INTERVAL="${PROCESS_CPU_SAMPLE_INTERVAL:-0.02}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
LEVELS="${LEVELS:-128 192 256}"

mkdir -p "$OUT_ROOT"

read_summary() {
    local file="$1"
    local key="$2"
    sed -n "s/^${key}=//p" "$file" | tr -d '\r' | tail -n 1
}

extract_field() {
    local file="$1"
    local key="$2"
    sed -n "s/.*${key}=\\([^ ]*\\).*/\\1/p" "$file" | tr -d '\r' | tail -n 1
}

best_count_for() {
    case "$1" in
        128) printf '256\n' ;;
        192) printf '128\n' ;;
        256) printf '128\n' ;;
        *) echo "bad level $1" >&2; exit 2 ;;
    esac
}

iters_for() {
    case "$1" in
        128) printf '3907\n' ;;
        192) printf '5000\n' ;;
        256) printf '3200\n' ;;
        *) echo "bad level $1" >&2; exit 2 ;;
    esac
}

cpu_baseline_summary_for() {
    local level="$1"
    find "$BASELINE_ROOT" -maxdepth 1 -type d -name "*_hqc${level}_cpu_scalar_process" -printf '%T@ %p\n' \
        | sort -nr \
        | awk 'NR == 1 {print $2 "/summary.env"}'
}

deploy_measure_scripts() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'"
    "$ADB" push "$ROOT_DIR/scripts/measure_board_energy.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" push "$ROOT_DIR/scripts/measure_process_cpu.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_board_energy.sh' '$ENERGY_REMOTE_DIR/measure_process_cpu.sh'"
}

build_and_deploy_worker() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_worker_best_${level}"

    ADB="$ADB" \
    HQC_USE_WORKER_POOL=1 \
    HQC_PARAM_LEVEL="$level" \
    HEXAGON_ARCH="$HEXAGON_ARCH" \
    HQC_PROJECT_DIR="$ROOT_DIR/labs/fastest" \
        bash "$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh" >/dev/null

    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi
    printf '%s\n' "$device_dir"
}

write_remote_workload() {
    local local_file="$1"
    local remote_file="$2"
    shift 2
    {
        printf '#!/system/bin/sh\n'
        printf 'set -eu\n'
        printf '%s\n' "$*"
    } > "$local_file"
    "$ADB" push "$local_file" "$remote_file" >/dev/null
    "$ADB" shell "chmod +x '$remote_file'"
}

run_direct_energy() {
    local level="$1" label="$2" workload="$3"
    local run_id out_dir log summary local_workload remote_workload

    run_id="$(date +%Y%m%d_%H%M%S)_${label}_direct"
    out_dir="$OUT_ROOT/$run_id"
    mkdir -p "$out_dir"
    log="$out_dir/direct_energy.log"
    summary="$out_dir/summary.env"
    local_workload="$out_dir/workload.sh"
    remote_workload="$ENERGY_REMOTE_DIR/${label}.workload.sh"
    write_remote_workload "$local_workload" "$remote_workload" "$workload"

    cat > "$out_dir/run.info" <<EOF_INFO
run_id=$run_id
label=$label
level=$level
mode=direct
workload_cmd=$workload
EOF_INFO

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && IDLE_POSITION='$DIRECT_IDLE_POSITION' IDLE_SECONDS='$DIRECT_IDLE_SECONDS' SAMPLE_INTERVAL='$DIRECT_SAMPLE_INTERVAL' sh ./measure_board_energy.sh '$label' sh '$remote_workload'" > "$log" 2>&1

    local rc result elapsed_s total us run_w idle_w delta_w delta_j uj run_samples idle_samples elapsed_ms dec_s tpw
    rc="$(extract_field "$log" rc)"
    result="$(extract_field "$log" result)"
    elapsed_s="$(extract_field "$log" elapsed_s)"
    total="$(extract_field "$log" total_decodes)"
    us="$(extract_field "$log" us_per_decode)"
    run_w="$(extract_field "$log" run_avg_W)"
    idle_w="$(extract_field "$log" idle_avg_W)"
    delta_w="$(extract_field "$log" delta_W)"
    delta_j="$(extract_field "$log" delta_energy_J)"
    uj="$(extract_field "$log" uJ_per_decode)"
    run_samples="$(extract_field "$log" run_samples)"
    idle_samples="$(extract_field "$log" idle_samples)"
    elapsed_ms="$(awk -v s="$elapsed_s" 'BEGIN {printf "%.3f", s * 1000.0}')"
    dec_s="$(awk -v us="$us" 'BEGIN {if (us > 0) printf "%.3f", 1000000.0 / us}')"
    tpw="$(awk -v t="$dec_s" -v w="$delta_w" 'BEGIN {if (w > 0) printf "%.3f", t / w}')"

    cat > "$summary" <<EOF_SUMMARY
run_id=$run_id
label=$label
mode=direct
workload_rc=$rc
result=$result
total_decodes=$total
elapsed_s=$elapsed_s
elapsed_ms=$elapsed_ms
us_per_decode=$us
decodes_per_s=$dec_s
run_avg_W=$run_w
idle_avg_W=$idle_w
delta_W=$delta_w
delta_energy_J=$delta_j
uJ_per_decode=$uj
throughput_per_W=$tpw
run_samples=$run_samples
idle_samples=$idle_samples
out_dir=$out_dir
EOF_SUMMARY
    printf '%s\n' "$summary"
}

run_process_cpu() {
    local level="$1" label="$2" workload="$3"
    local run_id out_dir log summary local_workload remote_workload

    run_id="$(date +%Y%m%d_%H%M%S)_${label}_process"
    out_dir="$OUT_ROOT/$run_id"
    mkdir -p "$out_dir"
    log="$out_dir/process_cpu.log"
    summary="$out_dir/summary.env"
    local_workload="$out_dir/process_workload.sh"
    remote_workload="$ENERGY_REMOTE_DIR/${label}.process.workload.sh"
    write_remote_workload "$local_workload" "$remote_workload" "$workload"

    cat > "$out_dir/run.info" <<EOF_INFO
run_id=$run_id
label=$label
level=$level
mode=process
workload_cmd=$workload
EOF_INFO

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && PROCESS_CPU_SAMPLE_INTERVAL='$PROCESS_CPU_SAMPLE_INTERVAL' PROCESS_CPU_TMP_DIR='$ENERGY_REMOTE_DIR' sh ./measure_process_cpu.sh '$label' sh '$remote_workload'" > "$log" 2>&1

    local rc result elapsed_s elapsed_ms total us cpu_s cpu_pct cpu_ms ticks hz samples
    rc="$(extract_field "$log" rc)"
    result="$(extract_field "$log" result)"
    elapsed_s="$(extract_field "$log" elapsed_s)"
    elapsed_ms="$(extract_field "$log" elapsed_ms)"
    total="$(extract_field "$log" total_decodes)"
    us="$(extract_field "$log" us_per_decode)"
    cpu_s="$(extract_field "$log" process_cpu_s)"
    cpu_pct="$(extract_field "$log" process_cpu_pct)"
    cpu_ms="$(extract_field "$log" cpu_ms_per_decode)"
    ticks="$(extract_field "$log" cpu_ticks)"
    hz="$(extract_field "$log" clk_tck)"
    samples="$(extract_field "$log" samples)"

    cat > "$summary" <<EOF_SUMMARY
run_id=$run_id
label=$label
mode=process
workload_rc=$rc
result=$result
total_decodes=$total
elapsed_s=$elapsed_s
elapsed_ms=$elapsed_ms
us_per_decode=$us
process_cpu_s=$cpu_s
process_cpu_pct=$cpu_pct
cpu_ms_per_decode=$cpu_ms
cpu_ticks=$ticks
clk_tck=$hz
process_samples=$samples
out_dir=$out_dir
EOF_SUMMARY
    printf '%s\n' "$summary"
}

direct_row() {
    local level="$1" summary="$2" backend="$3"
    printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$backend" \
        "$(read_summary "$summary" result)" \
        "$(read_summary "$summary" us_per_decode)" \
        "$(read_summary "$summary" decodes_per_s)" \
        "$(read_summary "$summary" delta_W)" \
        "$(read_summary "$summary" uJ_per_decode)" \
        "$(read_summary "$summary" throughput_per_W)" \
        "$(dirname "$summary")"
}

process_row() {
    local level="$1" summary="$2" baseline="$3" backend="$4"
    local reduction
    reduction="$(awk -v base="$(read_summary "$baseline" cpu_ms_per_decode)" -v cur="$(read_summary "$summary" cpu_ms_per_decode)" 'BEGIN {if (base > 0) printf "%.3f", 100.0 * (base - cur) / base}')"
    printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$backend" \
        "$(read_summary "$summary" result)" \
        "$(read_summary "$summary" total_decodes)" \
        "$(read_summary "$summary" elapsed_ms)" \
        "$(read_summary "$summary" us_per_decode)" \
        "$(read_summary "$summary" process_cpu_s)" \
        "$(read_summary "$summary" process_cpu_pct)" \
        "$(read_summary "$summary" cpu_ms_per_decode)" \
        "$reduction" \
        "$(dirname "$summary")"
}

direct_rows="$OUT_ROOT/direct_rows.md"
process_rows="$OUT_ROOT/process_rows.md"
raw_file="$OUT_ROOT/raw_result_lines.txt"
: > "$direct_rows"
: > "$process_rows"
: > "$raw_file"

deploy_measure_scripts

for level in $LEVELS; do
    count="$(best_count_for "$level")"
    iters="$(iters_for "$level")"
    label="hqc${level}_npu_worker_best_cw${count}"
    device_dir="$(build_and_deploy_worker "$level")"
    workload="cd $device_dir && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host buffer-bench rpcmem-cached worker $iters $count"
    process_workload="cd $device_dir && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && exec ./hqc_host buffer-bench rpcmem-cached worker $iters $count"
    direct_summary="$(run_direct_energy "$level" "$label" "$workload")"
    process_summary="$(run_process_cpu "$level" "$label" "$process_workload")"
    baseline_summary="$(cpu_baseline_summary_for "$level")"
    direct_row "$level" "$direct_summary" "NPU worker best cw=$count" >> "$direct_rows"
    process_row "$level" "$process_summary" "$baseline_summary" "NPU worker best cw=$count" >> "$process_rows"
    cat "$direct_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
    cat "$process_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
done

echo "direct_rows=$direct_rows"
echo "process_rows=$process_rows"
echo "raw=$raw_file"
