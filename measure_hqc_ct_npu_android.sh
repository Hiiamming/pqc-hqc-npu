#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/qprof_hqc_ct_runs}"
RESULT_MD="${RESULT_MD:-$ROOT_DIR/README_result_whole.md}"
PROFILE_TIME="${PROFILE_TIME:-30}"
STREAMING_RATE="${STREAMING_RATE:-1000}"
SAMPLING_RATE="${SAMPLING_RATE:-200}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v68}"
LEVELS="${LEVELS:-128 192 256}"
NPU_ITERS="${NPU_ITERS:-10000}"
DIRECT_SAMPLE_INTERVAL="${DIRECT_SAMPLE_INTERVAL:-0.1}"
DIRECT_IDLE_SECONDS="${DIRECT_IDLE_SECONDS:-10}"
DIRECT_IDLE_POSITION="${DIRECT_IDLE_POSITION:-both}"
ENERGY_REMOTE_DIR="${ENERGY_REMOTE_DIR:-/data/local/tmp/QDC_files/hqc_whole}"

if [ ! -x "$ADB" ]; then
    echo "ERROR: ADB executable not found: $ADB" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

read_summary() {
    local file="$1"
    local key="$2"
    sed -n "s/^${key}=//p" "$file" | tr -d '\r' | tail -n 1
}

latest_summary_for() {
    local label="$1"
    local mode="$2"
    find "$OUT_ROOT" -maxdepth 1 -type d -name "*_${label}_${mode}" -printf '%T@ %p\n' \
        | sort -nr \
        | awk 'NR == 1 {print $2 "/summary.env"}'
}

deploy_energy_script() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'"
    "$ADB" push "$ROOT_DIR/measure_board_energy.sh" "$ENERGY_REMOTE_DIR/measure_board_energy.sh" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_board_energy.sh'"
}

build_npu_ct() {
    local level="$1"
    ADB="$ADB" \
    HEXAGON_ARCH="$HEXAGON_ARCH" \
    HQC_PROJECT_DIR="$ROOT_DIR/hqc_lab_insintric_ct" \
    HQC_PARAM_LEVEL="$level" \
    HQC_DEFAULT_BENCH_ITERS="$NPU_ITERS" \
        bash "$ROOT_DIR/hqc_fastrpc_intrinsic/build_android_gcc_bionic.sh"
}

deploy_npu_ct() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_npu_ct"
    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/hqc_fastrpc_intrinsic/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/hqc_fastrpc_intrinsic/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi
    printf '%s\n' "$device_dir"
}

run_direct() {
    local label="$1"
    local workload="$2"
    local run_id
    run_id="$(date +%Y%m%d_%H%M%S)_${label}_direct"
    local out_dir="$OUT_ROOT/$run_id"
    mkdir -p "$out_dir"
    local remote_workload="$ENERGY_REMOTE_DIR/${label}.workload.sh"
    local remote_out="$ENERGY_REMOTE_DIR/${label}.energy"
    local local_workload="$out_dir/workload.sh"
    cat > "$local_workload" <<EOF_WORKLOAD
#!/system/bin/sh
set -e
$workload
EOF_WORKLOAD
    "$ADB" push "$local_workload" "$remote_workload" >/dev/null
    "$ADB" shell "chmod +x '$remote_workload'"
    set +e
    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && DIRECT_SAMPLE_INTERVAL='$DIRECT_SAMPLE_INTERVAL' DIRECT_IDLE_SECONDS='$DIRECT_IDLE_SECONDS' IDLE_POSITION='$DIRECT_IDLE_POSITION' ENERGY_TMP_DIR='$ENERGY_REMOTE_DIR' ./measure_board_energy.sh '$label' sh '$remote_workload' > '${remote_out}.log' 2>&1"
    local rc=$?
    set -e
    "$ADB" pull "${remote_out}.log" "$out_dir/direct_energy.log" >/dev/null 2>&1 || true
    "$ADB" pull "$ENERGY_REMOTE_DIR/${label}.run.samples" "$out_dir/run.samples" >/dev/null 2>&1 || true
    "$ADB" pull "$ENERGY_REMOTE_DIR/${label}.idle.samples" "$out_dir/idle.samples" >/dev/null 2>&1 || true
    if [ "$rc" -ne 0 ]; then
        echo "ERROR: direct energy run failed for $label, rc=$rc" >&2
        tail -50 "$out_dir/direct_energy.log" >&2 || true
        exit "$rc"
    fi
    awk -v out_dir="$out_dir" '
        /^\[energy-result\]/ {
            for (i = 1; i <= NF; ++i) {
                split($i, kv, "=")
                if (kv[1] == "label") label = kv[2]
                else if (kv[1] == "rc") workload_rc = kv[2]
                else if (kv[1] == "result") result = kv[2]
                else if (kv[1] == "elapsed_s") elapsed_s = kv[2]
                else if (kv[1] == "total_decodes") total_decodes = kv[2]
                else if (kv[1] == "us_per_decode") us_per_decode = kv[2]
                else if (kv[1] == "run_avg_W") run_avg_W = kv[2]
                else if (kv[1] == "idle_avg_W") idle_avg_W = kv[2]
                else if (kv[1] == "delta_W") delta_W = kv[2]
                else if (kv[1] == "delta_energy_J") delta_energy_J = kv[2]
                else if (kv[1] == "uJ_per_decode") uJ_per_decode = kv[2]
                else if (kv[1] == "run_samples") run_samples = kv[2]
                else if (kv[1] == "idle_samples") idle_samples = kv[2]
            }
        }
        END {
            elapsed_ms = elapsed_s * 1000.0
            decodes_per_s = total_decodes / elapsed_s
            throughput_per_W = (delta_W > 0) ? decodes_per_s / delta_W : 0
            printf "label=%s\n", label
            printf "mode=direct\n"
            printf "workload_rc=%s\n", workload_rc
            printf "result=%s\n", result
            printf "total_decodes=%s\n", total_decodes
            printf "elapsed_s=%.6f\n", elapsed_s
            printf "elapsed_ms=%.3f\n", elapsed_ms
            printf "us_per_decode=%s\n", us_per_decode
            printf "decodes_per_s=%.3f\n", decodes_per_s
            printf "run_avg_W=%s\n", run_avg_W
            printf "idle_avg_W=%s\n", idle_avg_W
            printf "delta_W=%s\n", delta_W
            printf "delta_energy_J=%s\n", delta_energy_J
            printf "uJ_per_decode=%s\n", uJ_per_decode
            printf "throughput_per_W=%.3f\n", throughput_per_W
            printf "run_samples=%s\n", run_samples
            printf "idle_samples=%s\n", idle_samples
            printf "out_dir=%s\n", out_dir
        }
    ' "$out_dir/direct_energy.log" > "$out_dir/summary.env"
    printf '%s\n' "$out_dir/summary.env"
}

run_idle() {
    local label="$1"
    PROFILE_TIME="$PROFILE_TIME" STREAMING_RATE="$STREAMING_RATE" SAMPLING_RATE="$SAMPLING_RATE" \
    OUT_ROOT="$OUT_ROOT" ADB="$ADB" \
        "$ROOT_DIR/measure_hqc_qprof_android.sh" idle "$label" >&2
    latest_summary_for "$label" idle
}

run_qprof() {
    local label="$1"
    local idle_power="$2"
    local workload="$3"
    PROFILE_TIME="$PROFILE_TIME" STREAMING_RATE="$STREAMING_RATE" SAMPLING_RATE="$SAMPLING_RATE" \
    OUT_ROOT="$OUT_ROOT" ADB="$ADB" IDLE_POWER_W="$idle_power" \
        "$ROOT_DIR/measure_hqc_qprof_android.sh" npu1 "$label" "$workload" >&2 || true
    latest_summary_for "$label" npu1
}

direct_row() {
    local level="$1"
    local summary="$2"
    local result total elapsed us dps idle run delta delta_j uj tp samples out_dir
    result="$(read_summary "$summary" result)"
    total="$(read_summary "$summary" total_decodes)"
    elapsed="$(read_summary "$summary" elapsed_ms)"
    us="$(read_summary "$summary" us_per_decode)"
    dps="$(read_summary "$summary" decodes_per_s)"
    idle="$(read_summary "$summary" idle_avg_W)"
    run="$(read_summary "$summary" run_avg_W)"
    delta="$(read_summary "$summary" delta_W)"
    delta_j="$(read_summary "$summary" delta_energy_J)"
    uj="$(read_summary "$summary" uJ_per_decode)"
    tp="$(read_summary "$summary" throughput_per_W)"
    samples="$(read_summary "$summary" run_samples)"
    out_dir="$(read_summary "$summary" out_dir)"
    printf '| HQC-%s | NPU CT | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$result" "$total" "$elapsed" "$us" "$dps" "$idle" "$run" "$delta" "$delta_j" "$uj" "$tp" "$samples" "$out_dir"
}

qprof_row() {
    local level="$1"
    local summary="$2"
    local rc result total elapsed us cpu npu qclk hmx memnoc thermal out_dir
    rc="$(read_summary "$summary" profile_rc)"
    result="$(read_summary "$summary" result)"
    total="$(read_summary "$summary" total_decodes)"
    elapsed="$(read_summary "$summary" elapsed_ms)"
    us="$(read_summary "$summary" us_per_decode)"
    cpu="$(read_summary "$summary" cpu_total_load_avg)"
    npu="$(read_summary "$summary" npu_util_percent_avg)"
    qclk="$(read_summary "$summary" qdsp_clock_MHz_avg)"
    hmx="$(read_summary "$summary" hmx_util_percent_avg)"
    memnoc="$(read_summary "$summary" memnoc_vote_MHz_avg)"
    thermal="$(read_summary "$summary" thermal_zone_C_max)"
    out_dir="$(read_summary "$summary" out_dir)"
    printf '| HQC-%s | NPU CT | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | `%s` |\n' \
        "$level" "$rc" "$result" "$total" "$elapsed" "$us" "$cpu" "$npu" "$qclk" "$hmx" "$memnoc" "$thermal" "$out_dir"
}

direct_rows="$OUT_ROOT/ct_direct_rows.md"
qprof_rows="$OUT_ROOT/ct_qprof_rows.md"
raw_file="$OUT_ROOT/ct_raw_summary.env"
: > "$direct_rows"
: > "$qprof_rows"
: > "$raw_file"

deploy_energy_script

for level in $LEVELS; do
    echo "[ct] === HQC-$level NPU CT, iters=$NPU_ITERS ==="
    build_npu_ct "$level"
    npu_dir="$(deploy_npu_ct "$level")"
    workload="cd $npu_dir && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host $NPU_ITERS"

    direct_summary="$(run_direct "hqc${level}_npu_ct" "$workload")"
    direct_row "$level" "$direct_summary" >> "$direct_rows"
    cat "$direct_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"

    idle_summary="$(run_idle "hqc${level}_idle_before_npu_ct")"
    idle_power="$(read_summary "$idle_summary" avg_power_W)"
    qprof_summary="$(run_qprof "hqc${level}_npu_ct" "$idle_power" "$workload")"
    qprof_row "$level" "$qprof_summary" >> "$qprof_rows"
    cat "$idle_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
    cat "$qprof_summary" >> "$raw_file"
    printf '\n' >> "$raw_file"
done

cat >> "$RESULT_MD" <<EOF_RESULT

## CT NPU Direct Energy Results

These rows measure \`hqc_lab_insintric_ct\` on cDSP through the same FastRPC harness. Scalar CPU rows are reused from the main table above.

| HQC | Backend | Result | Total decodes | elapsed ms | us/decode | decodes/s | idle W | run W | delta W | delta J | uJ/decode | decodes/s/W | run samples | Raw dir |
| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
$(cat "$direct_rows")

## CT NPU qprof Diagnostic Context

These qprof rows are diagnostic only; qprof perturbs power and clock state, so direct-energy rows above remain authoritative for energy.

| HQC | Backend | profile rc | Result | Total decodes | elapsed ms | us/decode | CPU load avg % | NPU util avg % | QDSP clk MHz | HMX util avg % | MemNoc MHz | Thermal max C | Raw dir |
| --- | --- | ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
$(cat "$qprof_rows")

### CT Raw Summary Files

\`\`\`text
$(cat "$raw_file")
\`\`\`
EOF_RESULT

echo "[ct] wrote CT NPU direct/qprof results to $RESULT_MD"
