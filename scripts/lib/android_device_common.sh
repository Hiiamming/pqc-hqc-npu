#!/usr/bin/env bash

# Shared primitives for Android real-device measurement entrypoints.
# Callers set ROOT_DIR before sourcing this file.

ANDROID_DEVICE_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="${ROOT_DIR:-$(cd "$ANDROID_DEVICE_COMMON_DIR/../.." && pwd)}"

android_device_require_adb() {
    if [ -x "$ADB" ] || command -v "$ADB" >/dev/null 2>&1; then
        return
    fi
    echo "ERROR: ADB executable not found: $ADB" >&2
    exit 1
}

android_device_read_summary() {
    local file="$1"
    local key="$2"
    sed -n "s/^${key}=//p" "$file" | tr -d '\r' | tail -n 1
}

android_device_extract_field() {
    local file="$1"
    local key="$2"
    sed -n "s/.*${key}=\\([^ ]*\\).*/\\1/p" "$file" | tr -d '\r' | tail -n 1
}

android_device_safe_label() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_'
}

hqc_fixture_count_for() {
    local level="$1"
    local header macro
    case "$level" in
        128) header="$ROOT_DIR/shared/fixtures/hqc1_decode_fixture.h"; macro="HQC1_FIXTURE_COUNT" ;;
        192) header="$ROOT_DIR/shared/fixtures/hqc3_decode_fixture.h"; macro="HQC3_FIXTURE_COUNT" ;;
        256) header="$ROOT_DIR/shared/fixtures/hqc5_decode_fixture.h"; macro="HQC5_FIXTURE_COUNT" ;;
        *) echo "ERROR: unknown HQC level '$level'" >&2; exit 1 ;;
    esac
    awk -v macro="$macro" '$2 == macro {print $3; found=1} END {if (!found) exit 1}' "$header"
}

android_device_build_cpu() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" HQC_PARAM_LEVEL="$level" HQC_BENCH_ITERS="$iters" \
        bash "$ROOT_DIR/runners/scalar_cpu/build_android_gcc_bionic.sh"
}

android_device_build_fastest() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" \
    HEXAGON_ARCH="$HEXAGON_ARCH" \
    HQC_PARAM_LEVEL="$level" \
    HQC_DEFAULT_BENCH_ITERS="$iters" \
    HQC_PROJECT_DIR="$ROOT_DIR/labs/fastest" \
        bash "$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh"
}

android_device_deploy_cpu() {
    local level="$1"
    local device_dir="$2"
    "$ADB" shell "mkdir -p '$device_dir'" >/dev/null
    "$ADB" push "$ROOT_DIR/runners/scalar_cpu/build/hqc${level}_decode_bench_arm64_android" "$device_dir/" >/dev/null
    printf '%s\n' "$device_dir"
}

android_device_deploy_fastrpc() {
    local device_dir="$1"
    "$ADB" shell "mkdir -p '$device_dir'" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi
    printf '%s\n' "$device_dir"
}

android_device_deploy_measure_scripts() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'" >/dev/null
    "$ADB" push "$ROOT_DIR/scripts/measure_board_energy.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" push "$ROOT_DIR/scripts/measure_process_cpu.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_board_energy.sh' '$ENERGY_REMOTE_DIR/measure_process_cpu.sh'" >/dev/null
}

android_device_write_remote_workload() {
    local local_file="$1"
    local remote_file="$2"
    local workload="$3"
    cat > "$local_file" <<EOF
#!/system/bin/sh
set -eu
$workload
EOF
    "$ADB" push "$local_file" "$remote_file" >/dev/null
    "$ADB" shell "chmod +x '$remote_file'" >/dev/null
}

android_device_fastrpc_workload() {
    local device_dir="$1"
    shift
    printf 'cd %q && chmod +x hqc_host && export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp" && export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic" && ' "$device_dir"
    printf '%s' "$*"
}

android_device_run_direct() {
    local label="$1"
    local workload="$2"
    local repeat="${3:-}"
    local safe run_id out_dir log summary local_script remote_script repeat_tag

    safe="$(android_device_safe_label "$label")"
    repeat_tag=""
    [ -n "$repeat" ] && repeat_tag="_r${repeat}"
    run_id="$(date +%Y%m%d_%H%M%S)${repeat_tag}_${safe}_direct"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/direct_energy.log"
    summary="$out_dir/summary.env"
    local_script="$out_dir/workload.sh"
    remote_script="$ENERGY_REMOTE_DIR/${safe}${repeat_tag}.direct.workload.sh"
    mkdir -p "$out_dir"
    android_device_write_remote_workload "$local_script" "$remote_script" "$workload"

    cat > "$out_dir/run.info" <<EOF
run_id=$run_id
label=$label
repeat=$repeat
mode=direct
sample_interval_s=$DIRECT_SAMPLE_INTERVAL
idle_seconds=$DIRECT_IDLE_SECONDS
idle_position=$DIRECT_IDLE_POSITION
workload_cmd=$workload
EOF

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && IDLE_POSITION='$DIRECT_IDLE_POSITION' IDLE_SECONDS='$DIRECT_IDLE_SECONDS' SAMPLE_INTERVAL='$DIRECT_SAMPLE_INTERVAL' ENERGY_TMP_DIR='$ENERGY_REMOTE_DIR' sh ./measure_board_energy.sh '$label' sh '$remote_script'" > "$log" 2>&1

    local rc result elapsed_s total us run_w idle_w delta_w delta_j uj run_samples idle_samples elapsed_ms dec_s tpw
    rc="$(android_device_extract_field "$log" rc)"
    result="$(android_device_extract_field "$log" result)"
    elapsed_s="$(android_device_extract_field "$log" elapsed_s)"
    total="$(android_device_extract_field "$log" total_decodes)"
    us="$(android_device_extract_field "$log" us_per_decode)"
    run_w="$(android_device_extract_field "$log" run_avg_W)"
    idle_w="$(android_device_extract_field "$log" idle_avg_W)"
    delta_w="$(android_device_extract_field "$log" delta_W)"
    delta_j="$(android_device_extract_field "$log" delta_energy_J)"
    uj="$(android_device_extract_field "$log" uJ_per_decode)"
    run_samples="$(android_device_extract_field "$log" run_samples)"
    idle_samples="$(android_device_extract_field "$log" idle_samples)"
    elapsed_ms="$(awk -v s="${elapsed_s:-0}" 'BEGIN {printf "%.3f", s * 1000.0}')"
    dec_s="$(awk -v us="${us:-0}" -v n="${total:-0}" -v s="${elapsed_s:-0}" 'BEGIN {if (us > 0) printf "%.3f", 1000000.0 / us; else if (s > 0) printf "%.3f", n / s}')"
    tpw="$(awk -v t="${dec_s:-0}" -v w="${delta_w:-0}" 'BEGIN {if (w > 0) printf "%.3f", t / w}')"

    cat > "$summary" <<EOF
run_id=$run_id
label=$label
repeat=$repeat
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
EOF
    printf '%s\n' "$summary"
}

android_device_run_process() {
    local label="$1"
    local workload="$2"
    local repeat="${3:-}"
    local safe run_id out_dir log summary local_script remote_script repeat_tag

    safe="$(android_device_safe_label "$label")"
    repeat_tag=""
    [ -n "$repeat" ] && repeat_tag="_r${repeat}"
    run_id="$(date +%Y%m%d_%H%M%S)${repeat_tag}_${safe}_process"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/process_cpu.log"
    summary="$out_dir/summary.env"
    local_script="$out_dir/process_workload.sh"
    remote_script="$ENERGY_REMOTE_DIR/${safe}${repeat_tag}.process.workload.sh"
    mkdir -p "$out_dir"
    android_device_write_remote_workload "$local_script" "$remote_script" "$workload"

    cat > "$out_dir/run.info" <<EOF
run_id=$run_id
label=$label
repeat=$repeat
mode=process
sample_interval_s=$PROCESS_CPU_SAMPLE_INTERVAL
workload_cmd=$workload
EOF

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && PROCESS_CPU_SAMPLE_INTERVAL='$PROCESS_CPU_SAMPLE_INTERVAL' PROCESS_CPU_TMP_DIR='$ENERGY_REMOTE_DIR' sh ./measure_process_cpu.sh '$label' sh '$remote_script'" > "$log" 2>&1

    local rc result elapsed_s elapsed_ms total us cpu_s cpu_pct cpu_ms ticks hz samples
    rc="$(android_device_extract_field "$log" rc)"
    result="$(android_device_extract_field "$log" result)"
    elapsed_s="$(android_device_extract_field "$log" elapsed_s)"
    elapsed_ms="$(android_device_extract_field "$log" elapsed_ms)"
    total="$(android_device_extract_field "$log" total_decodes)"
    us="$(android_device_extract_field "$log" us_per_decode)"
    cpu_s="$(android_device_extract_field "$log" process_cpu_s)"
    cpu_pct="$(android_device_extract_field "$log" process_cpu_pct)"
    cpu_ms="$(android_device_extract_field "$log" cpu_ms_per_decode)"
    ticks="$(android_device_extract_field "$log" cpu_ticks)"
    hz="$(android_device_extract_field "$log" clk_tck)"
    samples="$(android_device_extract_field "$log" samples)"
    if [ -z "$elapsed_ms" ]; then
        elapsed_ms="$(awk -v s="${elapsed_s:-0}" 'BEGIN {printf "%.3f", s * 1000.0}')"
    fi

    cat > "$summary" <<EOF
run_id=$run_id
label=$label
repeat=$repeat
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
EOF
    printf '%s\n' "$summary"
}

android_device_run_fastrpc_remote() {
    local device_dir="$1"
    local log="$2"
    shift 2

    mkdir -p "$(dirname "$log")"
    "$ADB" shell "$(android_device_fastrpc_workload "$device_dir" "./hqc_host $*")" | tee "$log"
}
