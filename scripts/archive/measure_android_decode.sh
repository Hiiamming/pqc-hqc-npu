#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/qprof/qprof_hqc_whole_runs}"
RESULT_MD="${RESULT_MD:-$ROOT_DIR/README_result_whole.md}"
PROFILE_TIME="${PROFILE_TIME:-30}"
STREAMING_RATE="${STREAMING_RATE:-1000}"
SAMPLING_RATE="${SAMPLING_RATE:-200}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v68}"
LEVELS="${LEVELS:-128 192 256}"
SKIP_BUILD="${SKIP_BUILD:-0}"
RUN_DIRECT_ENERGY="${RUN_DIRECT_ENERGY:-1}"
RUN_QPROF_CONTEXT="${RUN_QPROF_CONTEXT:-0}"
DIRECT_SAMPLE_INTERVAL="${DIRECT_SAMPLE_INTERVAL:-0.1}"
DIRECT_IDLE_SECONDS="${DIRECT_IDLE_SECONDS:-10}"
DIRECT_IDLE_POSITION="${DIRECT_IDLE_POSITION:-both}"
ENERGY_REMOTE_DIR="${ENERGY_REMOTE_DIR:-/data/local/tmp/QDC_files/hqc_whole}"
PROCESS_CPU_SAMPLE_INTERVAL="${PROCESS_CPU_SAMPLE_INTERVAL:-0.02}"

if [ ! -x "$ADB" ]; then
    echo "ERROR: ADB executable not found: $ADB" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

iters_for() {
    local kind="$1"
    local level="$2"
    local var="${kind}_ITERS_${level}"
    local all_var="${kind}_ITERS"
    if [ -n "${!var:-}" ]; then
        printf '%s\n' "${!var}"
    elif [ -n "${!all_var:-}" ]; then
        printf '%s\n' "${!all_var}"
    else
        case "$level" in
            128|192|256) printf '10000\n' ;;
            *) echo "ERROR: unknown HQC level '$level'" >&2; exit 1 ;;
        esac
    fi
}

latest_summary_for() {
    local label="$1"
    local mode="$2"
    find "$OUT_ROOT" -maxdepth 1 -type d -name "*_${label}_${mode}" -printf '%T@ %p\n' \
        | sort -nr \
        | awk 'NR == 1 {print $2 "/summary.env"}'
}

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

run_idle() {
    local label="$1"
    PROFILE_TIME="$PROFILE_TIME" \
    STREAMING_RATE="$STREAMING_RATE" \
    SAMPLING_RATE="$SAMPLING_RATE" \
    OUT_ROOT="$OUT_ROOT" \
    ADB="$ADB" \
        "$ROOT_DIR/scripts/measure_qprof.sh" idle "$label" >&2
    latest_summary_for "$label" idle
}

run_measured() {
    local mode="$1"
    local label="$2"
    local idle_power="$3"
    local workload="$4"
    PROFILE_TIME="$PROFILE_TIME" \
    STREAMING_RATE="$STREAMING_RATE" \
    SAMPLING_RATE="$SAMPLING_RATE" \
    OUT_ROOT="$OUT_ROOT" \
    ADB="$ADB" \
    IDLE_POWER_W="$idle_power" \
        "$ROOT_DIR/scripts/measure_qprof.sh" "$mode" "$label" "$workload" >&2
    latest_summary_for "$label" "$mode"
}

build_cpu() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" HQC_PARAM_LEVEL="$level" HQC_BENCH_ITERS="$iters" \
        bash "$ROOT_DIR/runners/scalar_cpu/build_android_gcc_bionic.sh"
}

deploy_cpu() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_cpu_scalar"
    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/runners/scalar_cpu/build/hqc${level}_decode_bench_arm64_android" "$device_dir/" >/dev/null
    printf '%s\n' "$device_dir"
}

build_npu_fastest() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" \
    HEXAGON_ARCH="$HEXAGON_ARCH" \
    HQC_PARAM_LEVEL="$level" \
    HQC_DEFAULT_BENCH_ITERS="$iters" \
    HQC_PROJECT_DIR="$ROOT_DIR/labs/fastest" \
        bash "$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh"
}

deploy_npu_fastest() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_npu_fastest_nonct"
    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi
    printf '%s\n' "$device_dir"
}

deploy_energy_script() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'"
    "$ADB" push "$ROOT_DIR/scripts/measure_board_energy.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_board_energy.sh'"
}

deploy_process_cpu_script() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'"
    "$ADB" push "$ROOT_DIR/scripts/measure_process_cpu.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_process_cpu.sh'"
}

run_direct() {
    local label="$1"
    local workload="$2"
    local safe_label run_id out_dir log summary workload_script remote_workload_script

    safe_label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9_.-' '_')"
    run_id="$(date +%Y%m%d_%H%M%S)_${safe_label}_direct"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/direct_energy.log"
    summary="$out_dir/summary.env"
    workload_script="$out_dir/workload.sh"
    remote_workload_script="$ENERGY_REMOTE_DIR/${safe_label}.workload.sh"
    mkdir -p "$out_dir"

    cat > "$workload_script" <<EOF
#!/system/bin/sh
set -eu
$workload
EOF
    "$ADB" push "$workload_script" "$remote_workload_script" >/dev/null
    "$ADB" shell "chmod +x '$remote_workload_script'"

    cat > "$out_dir/run.info" <<EOF
run_id=$run_id
label=$label
mode=direct
sample_interval_s=$DIRECT_SAMPLE_INTERVAL
idle_seconds=$DIRECT_IDLE_SECONDS
idle_position=$DIRECT_IDLE_POSITION
workload_cmd=$workload
EOF

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && IDLE_POSITION='$DIRECT_IDLE_POSITION' IDLE_SECONDS='$DIRECT_IDLE_SECONDS' SAMPLE_INTERVAL='$DIRECT_SAMPLE_INTERVAL' sh ./measure_board_energy.sh '$label' sh '$remote_workload_script'" > "$log" 2>&1

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

    elapsed_ms=""
    if [ -n "$elapsed_s" ]; then
        elapsed_ms="$(awk -v s="$elapsed_s" 'BEGIN {printf "%.3f", s * 1000.0}')"
    fi
    dec_s=""
    if [ -n "$us" ] && awk -v x="$us" 'BEGIN {exit !(x > 0)}'; then
        dec_s="$(awk -v us="$us" 'BEGIN {printf "%.3f", 1000000.0 / us}')"
    elif [ -n "$total" ] && [ -n "$elapsed_s" ]; then
        dec_s="$(awk -v n="$total" -v s="$elapsed_s" 'BEGIN {if (s > 0) printf "%.3f", n / s}')"
    fi
    tpw=""
    if [ -n "$dec_s" ] && [ -n "$delta_w" ]; then
        tpw="$(awk -v t="$dec_s" -v w="$delta_w" 'BEGIN {if (w > 0) printf "%.3f", t / w}')"
    fi

    cat > "$summary" <<EOF
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
EOF

    printf '%s\n' "$summary"
}

run_process_cpu() {
    local label="$1"
    local workload="$2"
    local safe_label run_id out_dir log summary workload_script remote_workload_script

    safe_label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9_.-' '_')"
    run_id="$(date +%Y%m%d_%H%M%S)_${safe_label}_process"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/process_cpu.log"
    summary="$out_dir/summary.env"
    workload_script="$out_dir/process_workload.sh"
    remote_workload_script="$ENERGY_REMOTE_DIR/${safe_label}.process.workload.sh"
    mkdir -p "$out_dir"

    cat > "$workload_script" <<EOF
#!/system/bin/sh
set -eu
$workload
EOF
    "$ADB" push "$workload_script" "$remote_workload_script" >/dev/null
    "$ADB" shell "chmod +x '$remote_workload_script'"

    cat > "$out_dir/run.info" <<EOF
run_id=$run_id
label=$label
mode=process
sample_interval_s=$PROCESS_CPU_SAMPLE_INTERVAL
workload_cmd=$workload
EOF

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && PROCESS_CPU_SAMPLE_INTERVAL='$PROCESS_CPU_SAMPLE_INTERVAL' PROCESS_CPU_TMP_DIR='$ENERGY_REMOTE_DIR' sh ./measure_process_cpu.sh '$label' sh '$remote_workload_script'" > "$log" 2>&1

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

    if [ -z "$elapsed_ms" ] && [ -n "$elapsed_s" ]; then
        elapsed_ms="$(awk -v s="$elapsed_s" 'BEGIN {printf "%.3f", s * 1000.0}')"
    fi

    cat > "$summary" <<EOF
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
EOF

    printf '%s\n' "$summary"
}

direct_row_from_summary() {
    local level="$1"
    local backend="$2"
    local summary="$3"
    local result total us dec_s run_w idle_w delta_w delta_j uj tpw elapsed samples out_dir

    result="$(read_summary "$summary" result)"
    total="$(read_summary "$summary" total_decodes)"
    us="$(read_summary "$summary" us_per_decode)"
    dec_s="$(read_summary "$summary" decodes_per_s)"
    run_w="$(read_summary "$summary" run_avg_W)"
    idle_w="$(read_summary "$summary" idle_avg_W)"
    delta_w="$(read_summary "$summary" delta_W)"
    delta_j="$(read_summary "$summary" delta_energy_J)"
    uj="$(read_summary "$summary" uJ_per_decode)"
    tpw="$(read_summary "$summary" throughput_per_W)"
    elapsed="$(read_summary "$summary" elapsed_ms)"
    samples="$(read_summary "$summary" run_samples)"
    out_dir="$(dirname "$summary")"
    printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$backend" "${result:-NA}" "${total:-}" "${elapsed:-}" "${us:-}" "${dec_s:-}" \
        "${idle_w:-}" "${run_w:-}" "${delta_w:-}" "${delta_j:-}" "${uj:-}" "${tpw:-}" "${samples:-}" "$out_dir"
}

qprof_power_valid() {
    local summary="$1"
    local power delta therm result
    power="$(read_summary "$summary" avg_power_W)"
    delta="$(read_summary "$summary" delta_W)"
    therm="$(read_summary "$summary" thermal_zone_C_max)"
    result="$(read_summary "$summary" result)"
    awk -v p="$power" -v d="$delta" -v t="$therm" -v r="$result" 'BEGIN {
        ok = (r == "PASS" && p > 0.1 && p < 20.0 && d > 0.0 && t > 0.0 && t < 130.0);
        print ok ? "yes" : "no";
    }'
}

thermal_valid() {
    local summary="$1"
    local therm
    therm="$(read_summary "$summary" thermal_zone_C_max)"
    awk -v t="$therm" 'BEGIN {print (t > 0.0 && t < 130.0) ? "yes" : "no"}'
}

npu_metrics_present() {
    local summary="$1"
    local npu qclk
    npu="$(read_summary "$summary" npu_util_percent_avg)"
    qclk="$(read_summary "$summary" qdsp_clock_MHz_avg)"
    if [ -n "$npu" ] || [ -n "$qclk" ]; then
        printf 'yes\n'
    else
        printf 'no\n'
    fi
}

qprof_row_from_summary() {
    local level="$1"
    local backend="$2"
    local summary="$3"
    local result cpu npu qclk hmx hmxclk memnoc therm pvalid tvalid nvalid out_dir
    result="$(read_summary "$summary" result)"
    cpu="$(read_summary "$summary" cpu_total_load_avg)"
    npu="$(read_summary "$summary" npu_util_percent_avg)"
    qclk="$(read_summary "$summary" qdsp_clock_MHz_avg)"
    hmx="$(read_summary "$summary" hmx_util_percent_avg)"
    hmxclk="$(read_summary "$summary" hmx_clock_MHz_avg)"
    memnoc="$(read_summary "$summary" memnoc_vote_MHz_avg)"
    therm="$(read_summary "$summary" thermal_zone_C_max)"
    pvalid="$(qprof_power_valid "$summary")"
    tvalid="$(thermal_valid "$summary")"
    nvalid="$(npu_metrics_present "$summary")"
    out_dir="$(dirname "$summary")"
    printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$backend" "${result:-NA}" "${cpu:-}" "${npu:-}" "${qclk:-}" "${hmx:-}" "${hmxclk:-}" \
        "${memnoc:-}" "${therm:-}" "$pvalid" "$tvalid/$nvalid" "$out_dir"
}

process_row_from_summary() {
    local level="$1"
    local backend="$2"
    local summary="$3"
    local baseline_summary="${4:-}"
    local result total elapsed us cpu_s cpu_pct cpu_ms reduction samples out_dir base_cpu_ms
    result="$(read_summary "$summary" result)"
    total="$(read_summary "$summary" total_decodes)"
    elapsed="$(read_summary "$summary" elapsed_ms)"
    us="$(read_summary "$summary" us_per_decode)"
    cpu_s="$(read_summary "$summary" process_cpu_s)"
    cpu_pct="$(read_summary "$summary" process_cpu_pct)"
    cpu_ms="$(read_summary "$summary" cpu_ms_per_decode)"
    samples="$(read_summary "$summary" process_samples)"
    out_dir="$(dirname "$summary")"
    reduction=""
    if [ -n "$baseline_summary" ]; then
        base_cpu_ms="$(read_summary "$baseline_summary" cpu_ms_per_decode)"
        if [ -n "$base_cpu_ms" ] && [ -n "$cpu_ms" ]; then
            reduction="$(awk -v base="$base_cpu_ms" -v cur="$cpu_ms" 'BEGIN {if (base > 0) printf "%.3f", 100.0 * (base - cur) / base}')"
        fi
    fi
    printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$backend" "${result:-NA}" "${total:-}" "${elapsed:-}" "${us:-}" \
        "${cpu_s:-}" "${cpu_pct:-}" "${cpu_ms:-}" "${reduction:-}" "${samples:-}" "$out_dir"
}

direct_rows_file="$OUT_ROOT/direct_rows.md"
process_rows_file="$OUT_ROOT/process_rows.md"
qprof_rows_file="$OUT_ROOT/qprof_rows.md"
raw_file="$OUT_ROOT/raw_result_lines.txt"
: > "$direct_rows_file"
: > "$process_rows_file"
: > "$qprof_rows_file"
: > "$raw_file"

write_results() {
    local status="${1:-complete}"
    cat > "$RESULT_MD" <<EOF
# HQC Whole-Device CPU vs NPU Results

Status: $status

Generated by:

\`\`\`sh
$(basename "$0")
\`\`\`

Device path uses Windows ADB through WSL:

\`\`\`text
$ADB
\`\`\`

Profiler settings:

- Direct energy source: Android power-supply voltage/current sampled by \`measure_board_energy.sh\`
- \`DIRECT_SAMPLE_INTERVAL=$DIRECT_SAMPLE_INTERVAL\`
- \`DIRECT_IDLE_SECONDS=$DIRECT_IDLE_SECONDS\`
- \`DIRECT_IDLE_POSITION=$DIRECT_IDLE_POSITION\`
- direct energy enabled: \`RUN_DIRECT_ENERGY=$RUN_DIRECT_ENERGY\`
- qprof context enabled: \`RUN_QPROF_CONTEXT=$RUN_QPROF_CONTEXT\`
- qprof \`PROFILE_TIME=$PROFILE_TIME\`, \`STREAMING_RATE=$STREAMING_RATE\`, \`SAMPLING_RATE=$SAMPLING_RATE\`
- Process CPU source: per-thread \`/proc/<pid>/task/*/stat\` ticks sampled by \`measure_process_cpu.sh\`
- \`PROCESS_CPU_SAMPLE_INTERVAL=$PROCESS_CPU_SAMPLE_INTERVAL\`
- Build/deploy artifacts skipped: \`SKIP_BUILD=$SKIP_BUILD\`
- NPU profiler capability: \`profiler:nsp1-dsp-metrics\`
- CPU path: scalar ARM64 baseline built as an Android/Bionic PIE executable
- NPU path: current \`labs/fastest\` FastRPC build

Energy numbers in the main table use direct device power-supply samples with qprof disabled. qprof can perturb power and clock state, so qprof runs are diagnostic-only and are never mixed into the direct-energy table.

## Direct Energy Results

| HQC | Backend | Result | Total decodes | elapsed ms | us/decode | decodes/s | idle W | run W | delta W | delta J | uJ/decode | decodes/s/W | run samples | Raw dir |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
$(cat "$direct_rows_file")

## Process CPU Offload Results

These rows measure CPU time consumed by the decoder process itself. qprof CPU metrics are whole-system diagnostics and are not used for offload conclusions.

| HQC | Backend | Result | Total decodes | elapsed ms | us/decode | process CPU s | process CPU % | CPU ms/decode | Reduction vs CPU % | samples | Raw dir |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
$(cat "$process_rows_file")

## qprof Diagnostic Context

| HQC | Backend | Result | CPU load avg % | NPU util avg % | QDSP clk MHz | HMX util avg % | HMX clk MHz | MemNoc MHz | Thermal max C | qprof power valid | thermal/NPU valid | Raw dir |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
$(cat "$qprof_rows_file")

## Raw Summary Files

\`\`\`text
$(cat "$raw_file")
\`\`\`
EOF
}

trap 'write_results "partial: measurement aborted before all cases completed"; echo "[whole] wrote partial $RESULT_MD" >&2' EXIT

echo "[whole] ADB=$ADB"
echo "[whole] OUT_ROOT=$OUT_ROOT"
echo "[whole] RESULT_MD=$RESULT_MD"
echo "[whole] LEVELS=$LEVELS"
deploy_energy_script
deploy_process_cpu_script

for level in $LEVELS; do
    cpu_iters="$(iters_for CPU "$level")"
    npu_iters="$(iters_for NPU "$level")"

    echo "[whole] === HQC-$level CPU scalar, iters=$cpu_iters ==="
    if [ "$SKIP_BUILD" = "1" ]; then
        cpu_dir="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_cpu_scalar"
    else
        build_cpu "$level" "$cpu_iters"
        cpu_dir="$(deploy_cpu "$level")"
    fi
    cpu_workload="cd $cpu_dir && chmod +x hqc${level}_decode_bench_arm64_android && ./hqc${level}_decode_bench_arm64_android"
    cpu_process_workload="cd $cpu_dir && chmod +x hqc${level}_decode_bench_arm64_android && exec ./hqc${level}_decode_bench_arm64_android"
    if [ "$RUN_DIRECT_ENERGY" = "1" ]; then
        cpu_direct_summary="$(run_direct "hqc${level}_cpu_scalar" "$cpu_workload")"
        direct_row_from_summary "$level" "CPU scalar" "$cpu_direct_summary" >> "$direct_rows_file"
        cat "$cpu_direct_summary" >> "$raw_file"
        printf '\n' >> "$raw_file"
    fi
    cpu_process_summary="$(run_process_cpu "hqc${level}_cpu_scalar" "$cpu_process_workload")"
    process_row_from_summary "$level" "CPU scalar" "$cpu_process_summary" "$cpu_process_summary" >> "$process_rows_file"
    cat "$cpu_process_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
    if [ "$RUN_QPROF_CONTEXT" = "1" ]; then
        idle_summary="$(run_idle "hqc${level}_idle_before_cpu")"
        idle_power="$(read_summary "$idle_summary" avg_power_W)"
        cpu_summary="$(run_measured cpu "hqc${level}_cpu_scalar" "$idle_power" "$cpu_workload")"
        qprof_row_from_summary "$level" "CPU scalar" "$cpu_summary" >> "$qprof_rows_file"
        cat "$cpu_summary" >> "$raw_file"
        printf '\n' >> "$raw_file"
    fi

    echo "[whole] === HQC-$level NPU fastest non-CT, iters=$npu_iters ==="
    if [ "$SKIP_BUILD" = "1" ]; then
        npu_dir="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_npu_fastest_nonct"
    else
        build_npu_fastest "$level" "$npu_iters"
        npu_dir="$(deploy_npu_fastest "$level")"
    fi
    npu_workload="cd $npu_dir && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host $npu_iters"
    npu_process_workload="cd $npu_dir && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && exec ./hqc_host $npu_iters"
    if [ "$RUN_DIRECT_ENERGY" = "1" ]; then
        npu_direct_summary="$(run_direct "hqc${level}_npu_fastest_nonct" "$npu_workload")"
        direct_row_from_summary "$level" "NPU fastest non-CT" "$npu_direct_summary" >> "$direct_rows_file"
        cat "$npu_direct_summary" >> "$raw_file"
        printf '\n' >> "$raw_file"
    fi
    npu_process_summary="$(run_process_cpu "hqc${level}_npu_fastest_nonct" "$npu_process_workload")"
    process_row_from_summary "$level" "NPU fastest non-CT" "$npu_process_summary" "$cpu_process_summary" >> "$process_rows_file"
    cat "$npu_process_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
    if [ "$RUN_QPROF_CONTEXT" = "1" ]; then
        idle_summary="$(run_idle "hqc${level}_idle_before_npu")"
        idle_power="$(read_summary "$idle_summary" avg_power_W)"
        npu_summary="$(run_measured npu1 "hqc${level}_npu_fastest_nonct" "$idle_power" "$npu_workload")"
        qprof_row_from_summary "$level" "NPU fastest non-CT" "$npu_summary" >> "$qprof_rows_file"
        cat "$npu_summary" >> "$raw_file"
        printf '\n' >> "$raw_file"
    fi
done

trap - EXIT
write_results "complete"
echo "[whole] wrote $RESULT_MD"
