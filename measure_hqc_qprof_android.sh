#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat >&2 <<'EOF'
usage:
  measure_hqc_qprof_android.sh idle LABEL
  measure_hqc_qprof_android.sh cpu LABEL DEVICE_WORKLOAD_COMMAND
  measure_hqc_qprof_android.sh npu0 LABEL DEVICE_WORKLOAD_COMMAND
  measure_hqc_qprof_android.sh npu1 LABEL DEVICE_WORKLOAD_COMMAND

Runs Qualcomm Profiler through Windows adb.exe, optionally runs one Android
workload during the profiling window, saves logs locally, and prints one
[hqc-qprof-result] line.

Environment:
  ADB                 adb executable. Default: /mnt/c/Temp/ADB/platform-tools/adb.exe
  OUT_ROOT            local output root. Default: qprof_hqc_runs
  PROFILE_TIME        qprof duration in seconds. Default: 30
  PROFILE_START_DELAY seconds to wait before launching workload. Default: 2
  STREAMING_RATE      qprof streaming rate in ms. Default: 1000
  SAMPLING_RATE       qprof sampling rate in ms. Default: 200
  IDLE_POWER_W        optional idle baseline power for delta energy math
  QPROF_BIN           qprof binary on device. Default: /vendor/bin/qprof
  QPROF_LIB_DIR       qprof shared libs on device. Default: /vendor/qprof/libs
  QPROF_BACKEND_DIR   qprof backend libs on device. Default: /vendor/qprof/backends

Examples:
  PROFILE_TIME=30 ./measure_hqc_qprof_android.sh idle hdk8550_idle

  PROFILE_TIME=30 IDLE_POWER_W=0.70 ./measure_hqc_qprof_android.sh cpu hdk8550_cpu \
    'cd /data/local/tmp/QDC_files/hdk8550_cpu && ./hqc128_decode_bench_arm64_static'

  PROFILE_TIME=30 IDLE_POWER_W=0.70 ./measure_hqc_qprof_android.sh npu1 hdk8550_npu1 \
    'cd /data/local/tmp/QDC_files/hdk8550_npu && ./hqc_npu_decoder 10000'
EOF
}

if [ "$#" -lt 2 ]; then
    usage
    exit 2
fi

mode="$1"
label="$2"
shift 2
workload_cmd="$*"

case "$mode" in
    idle|cpu|npu0|npu1) ;;
    *)
        echo "ERROR: unknown mode '$mode'. Use idle, cpu, npu0, or npu1." >&2
        exit 2
        ;;
esac

if [ "$mode" != "idle" ] && [ -z "$workload_cmd" ]; then
    echo "ERROR: DEVICE_WORKLOAD_COMMAND is required for mode '$mode'." >&2
    usage
    exit 2
fi

ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
OUT_ROOT="${OUT_ROOT:-qprof_hqc_runs}"
PROFILE_TIME="${PROFILE_TIME:-30}"
PROFILE_START_DELAY="${PROFILE_START_DELAY:-2}"
STREAMING_RATE="${STREAMING_RATE:-1000}"
SAMPLING_RATE="${SAMPLING_RATE:-200}"
IDLE_POWER_W="${IDLE_POWER_W:-}"
QPROF_BIN="${QPROF_BIN:-/vendor/bin/qprof}"
QPROF_LIB_DIR="${QPROF_LIB_DIR:-/vendor/qprof/libs}"
QPROF_BACKEND_DIR="${QPROF_BACKEND_DIR:-/vendor/qprof/backends}"

if [ ! -x "$ADB" ]; then
    if command -v adb >/dev/null 2>&1; then
        ADB="$(command -v adb)"
    else
        echo "ERROR: adb not found. Set ADB=/path/to/adb.exe." >&2
        exit 1
    fi
fi

safe_label="$(printf '%s' "$label" | tr -c 'A-Za-z0-9_.-' '_')"
run_id="$(date +%Y%m%d_%H%M%S)_${safe_label}_${mode}"
out_dir="$OUT_ROOT/$run_id"
remote_result_dir="/data/shared/QualcommProfiler/profilingresults/$run_id"

mkdir -p "$out_dir"

qprof_env="export QMONITOR_BACKEND_LIB_PATH='$QPROF_BACKEND_DIR'; export LD_LIBRARY_PATH=\$LD_LIBRARY_PATH:'$QPROF_LIB_DIR'"

cpu_caps='profiler:apps-proc-cpu-metrics profiler:apps-proc-battery-metrics profiler:apps-proc-thermal-metrics'
cpu_metrics='4616 4696 4720 4618 4619 4620 4621 4622 4623 4624 4703 4705 4708 6464 6465'

case "$mode" in
    idle|cpu)
        caps="$cpu_caps"
        metrics="$cpu_metrics"
        ;;
    npu0)
        caps='profiler:nsp-dsp-metrics profiler:apps-proc-cpu-metrics profiler:apps-proc-battery-metrics profiler:apps-proc-thermal-metrics'
        metrics='4096 4097 4098 4099 4182 4183 4184 4480 4481 4521 4496 4497 4498 4502 4503 4504 4505 4506 4507 4703 4705 4708 4616 4696 6464 6465'
        ;;
    npu1)
        caps='profiler:nsp1-dsp-metrics profiler:apps-proc-cpu-metrics profiler:apps-proc-battery-metrics profiler:apps-proc-thermal-metrics'
        metrics='4096 4097 4098 4099 4182 4183 4184 4480 4481 4521 4496 4497 4498 4502 4503 4504 4505 4506 4507 4703 4705 4708 4616 4696 6464 6465'
        ;;
esac

profile_log="$out_dir/qprof.log"
profile_clean_log="$out_dir/qprof.clean.log"
workload_log="$out_dir/workload.log"
summary_env="$out_dir/summary.env"

cat > "$out_dir/run.info" <<EOF
run_id=$run_id
label=$label
mode=$mode
profile_time=$PROFILE_TIME
profile_start_delay=$PROFILE_START_DELAY
streaming_rate=$STREAMING_RATE
sampling_rate=$SAMPLING_RATE
idle_power_w=$IDLE_POWER_W
caps=$caps
metrics=$metrics
remote_result_dir=$remote_result_dir
workload_cmd=$workload_cmd
EOF

profile_cmd="$qprof_env; rm -rf '$remote_result_dir'; mkdir -p '$remote_result_dir'; '$QPROF_BIN' --profile --profile-type async --file-format json --capabilities-list $caps --metric-id-list $metrics --profile-time '$PROFILE_TIME' --streaming-rate '$STREAMING_RATE' --sampling-rate '$SAMPLING_RATE' --live --result-format verbose --summary --result-dir-path '$remote_result_dir'"

echo "[hqc-qprof] run_id=$run_id"
echo "[hqc-qprof] mode=$mode"
echo "[hqc-qprof] out_dir=$out_dir"
echo "[hqc-qprof] remote_result_dir=$remote_result_dir"
echo "[hqc-qprof] starting profiler"

set +e
"$ADB" shell "$profile_cmd" > "$profile_log" 2>&1 &
profile_pid=$!
sleep "$PROFILE_START_DELAY"

workload_rc=0
if [ "$mode" != "idle" ]; then
    echo "[hqc-qprof] starting workload"
    "$ADB" shell "$workload_cmd" > "$workload_log" 2>&1
    workload_rc=$?
else
    : > "$workload_log"
fi

wait "$profile_pid"
profile_rc=$?
set -e

"$ADB" pull "$remote_result_dir" "$out_dir/device_results" >/dev/null 2>&1 || true

sed -r $'s/\x1B\\[[0-9;]*[[:alpha:]]//g' "$profile_log" > "$profile_clean_log" || cp "$profile_log" "$profile_clean_log"

metric_values_awk='
function metric_value(line,    n, parts, v) {
    gsub(/\r/, "", line)
    n = split(line, parts, ":")
    v = parts[n]
    sub(/^[^0-9.+-]*/, "", v)
    sub(/[[:space:]].*$/, "", v)
    return v
}
$0 ~ /^Timestamp:/ && $0 ~ ("Metric ID:" id "([^0-9]|$)") {
    v = metric_value($0)
    if (v != "" && v ~ /^[-+]?[0-9.]+$/) {
        print v
    }
}'

metric_avg() {
    awk -v id="$1" "$metric_values_awk" "$profile_clean_log" \
        | awk '{sum += $1; count += 1} END {if (count > 0) printf "%.6f", sum / count}'
}

metric_min() {
    awk -v id="$1" "$metric_values_awk" "$profile_clean_log" \
        | awk 'NR == 1 {min = $1} $1 < min {min = $1} END {if (NR > 0) printf "%.6f", min}'
}

metric_max() {
    awk -v id="$1" "$metric_values_awk" "$profile_clean_log" \
        | awk 'NR == 1 {max = $1} $1 > max {max = $1} END {if (NR > 0) printf "%.6f", max}'
}

extract_workload_field() {
    sed -n "s/.*$1=\\([^ ]*\\).*/\\1/p" "$workload_log" | tr -d '\r' | tail -n 1
}

result="$(extract_workload_field result)"
total_decodes="$(extract_workload_field total_decodes)"
us_per_decode="$(extract_workload_field us_per_decode)"
elapsed_ms="$(extract_workload_field elapsed_ms)"

if [ -z "$elapsed_ms" ]; then
    elapsed_ms="$(awk -F'elapsed_ms=' '/elapsed_ms=/{split($2,a," "); v=a[1]} END {if (v != "") print v}' "$workload_log")"
fi

voltage_avg="$(metric_avg 4703)"
current_avg="$(metric_avg 4705)"
cpu_total_avg="$(metric_avg 4616)"
cpu_eff_avg="$(metric_avg 4696)"
thermal_avg="$(metric_avg 6464)"
thermal_max="$(metric_max 6464)"
npu_mpps_avg="$(metric_avg 4096)"
npu_load_avg="$(metric_avg 4097)"
npu_util_avg="$(metric_avg 4098)"
npu_pcpp_avg="$(metric_avg 4099)"
qdsp_clk_avg="$(metric_avg 4182)"
memnoc_vote_avg="$(metric_avg 4183)"
hmx_util_avg="$(metric_avg 4480)"
hmx_active_avg="$(metric_avg 4481)"
hmx_clk_avg="$(metric_avg 4521)"
udma_active_avg="$(metric_avg 4496)"

avg_power_w=""
if [ -n "$voltage_avg" ] && [ -n "$current_avg" ]; then
    avg_power_w="$(awk -v v="$voltage_avg" -v i="$current_avg" 'BEGIN {if (i < 0) i = -i; printf "%.9f", (v * i) / 1000000000000.0}')"
fi

decodes_per_s=""
if [ -n "$us_per_decode" ] && awk -v x="$us_per_decode" 'BEGIN {exit !(x > 0)}'; then
    decodes_per_s="$(awk -v us="$us_per_decode" 'BEGIN {printf "%.3f", 1000000.0 / us}')"
elif [ -n "$total_decodes" ] && [ -n "$elapsed_ms" ]; then
    decodes_per_s="$(awk -v n="$total_decodes" -v ms="$elapsed_ms" 'BEGIN {if (ms > 0) printf "%.3f", n / (ms / 1000.0)}')"
fi

delta_w=""
delta_energy_j=""
uj_per_decode=""
throughput_per_w=""
if [ -n "$IDLE_POWER_W" ] && [ -n "$avg_power_w" ]; then
    delta_w="$(awk -v run="$avg_power_w" -v idle="$IDLE_POWER_W" 'BEGIN {printf "%.9f", run - idle}')"
    if [ -n "$elapsed_ms" ]; then
        delta_energy_j="$(awk -v dw="$delta_w" -v ms="$elapsed_ms" 'BEGIN {printf "%.9f", dw * (ms / 1000.0)}')"
    fi
    if [ -n "$delta_energy_j" ] && [ -n "$total_decodes" ]; then
        uj_per_decode="$(awk -v e="$delta_energy_j" -v n="$total_decodes" 'BEGIN {if (n > 0) printf "%.3f", e * 1000000.0 / n}')"
    fi
    if [ -n "$decodes_per_s" ]; then
        throughput_per_w="$(awk -v t="$decodes_per_s" -v dw="$delta_w" 'BEGIN {if (dw > 0) printf "%.3f", t / dw}')"
    fi
fi

cat > "$summary_env" <<EOF
run_id=$run_id
label=$label
mode=$mode
profile_rc=$profile_rc
workload_rc=$workload_rc
result=$result
total_decodes=$total_decodes
elapsed_ms=$elapsed_ms
us_per_decode=$us_per_decode
decodes_per_s=$decodes_per_s
avg_power_W=$avg_power_w
idle_power_W=$IDLE_POWER_W
delta_W=$delta_w
delta_energy_J=$delta_energy_j
uJ_per_decode=$uj_per_decode
throughput_per_W=$throughput_per_w
cpu_total_load_avg=$cpu_total_avg
cpu_effective_util_avg=$cpu_eff_avg
npu_mpps_avg=$npu_mpps_avg
npu_load_MCPS_avg=$npu_load_avg
npu_util_percent_avg=$npu_util_avg
npu_pcpp_avg=$npu_pcpp_avg
qdsp_clock_MHz_avg=$qdsp_clk_avg
memnoc_vote_MHz_avg=$memnoc_vote_avg
hmx_util_percent_avg=$hmx_util_avg
hmx_active_MCPS_avg=$hmx_active_avg
hmx_clock_MHz_avg=$hmx_clk_avg
udma_active_MCPS_avg=$udma_active_avg
thermal_zone_C_avg=$thermal_avg
thermal_zone_C_max=$thermal_max
out_dir=$out_dir
EOF

printf '[hqc-qprof-result] label=%s mode=%s profile_rc=%s workload_rc=%s result=%s total_decodes=%s elapsed_ms=%s us_per_decode=%s decodes_per_s=%s avg_power_W=%s idle_power_W=%s delta_W=%s delta_energy_J=%s uJ_per_decode=%s throughput_per_W=%s cpu_total_load_avg=%s cpu_effective_util_avg=%s npu_util_percent_avg=%s qdsp_clock_MHz_avg=%s hmx_util_percent_avg=%s hmx_clock_MHz_avg=%s memnoc_vote_MHz_avg=%s thermal_zone_C_max=%s out_dir=%s\n' \
    "$label" "$mode" "$profile_rc" "$workload_rc" "$result" "$total_decodes" "$elapsed_ms" "$us_per_decode" "$decodes_per_s" \
    "$avg_power_w" "$IDLE_POWER_W" "$delta_w" "$delta_energy_j" "$uj_per_decode" "$throughput_per_w" \
    "$cpu_total_avg" "$cpu_eff_avg" "$npu_util_avg" "$qdsp_clk_avg" "$hmx_util_avg" "$hmx_clk_avg" "$memnoc_vote_avg" "$thermal_max" "$out_dir"

if [ "$profile_rc" -ne 0 ] || [ "$workload_rc" -ne 0 ]; then
    exit 1
fi
