#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/qprof/qprof_hqc_whole_stats_$RUN_ID}"
RESULT_MD="${RESULT_MD:-$ROOT_DIR/README_result_whole.md}"
RESULT_TEX="${RESULT_TEX:-$OUT_ROOT/real_device_stats_tables.tex}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
LEVELS="${LEVELS:-128 192 256}"
REPEATS="${REPEATS:-5}"
SANITY_ONLY="${SANITY_ONLY:-0}"
RUN_SANITY="${RUN_SANITY:-1}"
SKIP_BUILD="${SKIP_BUILD:-0}"
RUN_DIRECT_ENERGY="${RUN_DIRECT_ENERGY:-1}"
DIRECT_SAMPLE_INTERVAL="${DIRECT_SAMPLE_INTERVAL:-0.1}"
DIRECT_IDLE_SECONDS="${DIRECT_IDLE_SECONDS:-10}"
DIRECT_IDLE_POSITION="${DIRECT_IDLE_POSITION:-both}"
ENERGY_REMOTE_DIR="${ENERGY_REMOTE_DIR:-/data/local/tmp/QDC_files/hqc_whole_stats}"
PROCESS_CPU_SAMPLE_INTERVAL="${PROCESS_CPU_SAMPLE_INTERVAL:-0.02}"
CPU_TARGET_DECODES="${CPU_TARGET_DECODES:-32000}"
NPU_TARGET_DECODES="${NPU_TARGET_DECODES:-32000}"
CPU_LATENCY_TOLERANCE_PCT="${CPU_LATENCY_TOLERANCE_PCT:-10}"
NPU_LATENCY_TOLERANCE_PCT="${NPU_LATENCY_TOLERANCE_PCT:-15}"
NPU_ENERGY_TOLERANCE_PCT="${NPU_ENERGY_TOLERANCE_PCT:-30}"

if [ ! -x "$ADB" ]; then
    echo "ERROR: ADB executable not found: $ADB" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

SUMMARY_CSV="$OUT_ROOT/summary.csv"
AGG_DIRECT_CSV="$OUT_ROOT/aggregate_direct.csv"
AGG_PROCESS_CSV="$OUT_ROOT/aggregate_process.csv"
SANITY_CSV="$OUT_ROOT/sanity.csv"
RAW_FILE="$OUT_ROOT/raw_summary_files.txt"

cat > "$SUMMARY_CSV" <<'EOF'
repeat,level,backend,mode,result,total_decodes,elapsed_ms,us_per_decode,decodes_per_s,idle_W,run_W,delta_W,delta_energy_J,uJ_per_decode,throughput_per_W,process_cpu_s,process_cpu_pct,cpu_ms_per_decode,reduction_vs_cpu_pct,samples,out_dir
EOF
: > "$RAW_FILE"
cat > "$SANITY_CSV" <<'EOF'
level,backend,result,metric,reference,measured,diff_pct,tolerance_pct,status,out_dir
EOF

baseline_value() {
    local level="$1"
    local backend="$2"
    local metric="$3"
    case "$level:$backend:$metric" in
        128:CPU\ scalar:us_per_decode) printf '80.529\n' ;;
        128:CPU\ scalar:uJ_per_decode) printf '192.122\n' ;;
        128:CPU\ scalar:throughput_per_W) printf '5247.589\n' ;;
        128:NPU\ fastest\ non-CT:us_per_decode) printf '37.106\n' ;;
        128:NPU\ fastest\ non-CT:uJ_per_decode) printf '14.401\n' ;;
        128:NPU\ fastest\ non-CT:throughput_per_W) printf '72397.476\n' ;;
        192:CPU\ scalar:us_per_decode) printf '102.903\n' ;;
        192:CPU\ scalar:uJ_per_decode) printf '249.029\n' ;;
        192:CPU\ scalar:throughput_per_W) printf '4051.094\n' ;;
        192:NPU\ fastest\ non-CT:us_per_decode) printf '49.849\n' ;;
        192:NPU\ fastest\ non-CT:uJ_per_decode) printf '20.653\n' ;;
        192:NPU\ fastest\ non-CT:throughput_per_W) printf '49597.600\n' ;;
        256:CPU\ scalar:us_per_decode) printf '226.955\n' ;;
        256:CPU\ scalar:uJ_per_decode) printf '581.722\n' ;;
        256:CPU\ scalar:throughput_per_W) printf '1726.476\n' ;;
        256:NPU\ fastest\ non-CT:us_per_decode) printf '80.309\n' ;;
        256:NPU\ fastest\ non-CT:uJ_per_decode) printf '26.636\n' ;;
        256:NPU\ fastest\ non-CT:throughput_per_W) printf '38363.340\n' ;;
        *) return 1 ;;
    esac
}

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
        if [ "$kind" = "NPU" ] && [ -z "${NPU_TARGET_DECODES:-}" ]; then
            case "$level" in
                128) printf '1000\n' ;;
                192) printf '100\n' ;;
                256) printf '50\n' ;;
                *) echo "ERROR: unknown HQC level '$level'" >&2; exit 1 ;;
            esac
            return
        fi
        local target_var="${kind}_TARGET_DECODES"
        local target="${!target_var:-32000}"
        local fixtures
        fixtures="$(fixture_count_for "$level")"
        awk -v target="$target" -v fixtures="$fixtures" 'BEGIN {
            iters = int((target + fixtures - 1) / fixtures);
            if (iters < 1) iters = 1;
            print iters;
        }'
    fi
}

fixture_count_for() {
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

safe_label() {
    printf '%s' "$1" | tr -c 'A-Za-z0-9_.-' '_'
}

build_cpu() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" HQC_PARAM_LEVEL="$level" HQC_BENCH_ITERS="$iters" \
        bash "$ROOT_DIR/runners/scalar_cpu/build_android_gcc_bionic.sh"
}

deploy_cpu() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_whole_stats/hqc${level}_cpu_scalar"
    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/runners/scalar_cpu/build/hqc${level}_decode_bench_arm64_android" "$device_dir/" >/dev/null
    printf '%s\n' "$device_dir"
}

build_npu() {
    local level="$1"
    local iters="$2"
    ADB="$ADB" \
    HEXAGON_ARCH="$HEXAGON_ARCH" \
    HQC_PARAM_LEVEL="$level" \
    HQC_DEFAULT_BENCH_ITERS="$iters" \
    HQC_PROJECT_DIR="$ROOT_DIR/labs/fastest" \
        bash "$ROOT_DIR/fastrpc/hqc/build_android_gcc_bionic.sh"
}

deploy_npu() {
    local level="$1"
    local device_dir="/data/local/tmp/QDC_files/hqc_whole_stats/hqc${level}_npu_fastest_nonct"
    "$ADB" shell "mkdir -p '$device_dir'"
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/hqc_host" "$device_dir/" >/dev/null
    "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/libhqc_skel.so" "$device_dir/" >/dev/null
    if [ -f "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" ]; then
        "$ADB" push "$ROOT_DIR/fastrpc/hqc/build/testsig-0xaa3ec42e.so" "$device_dir/" >/dev/null
    fi
    printf '%s\n' "$device_dir"
}

deploy_measure_scripts() {
    "$ADB" shell "mkdir -p '$ENERGY_REMOTE_DIR'"
    "$ADB" push "$ROOT_DIR/scripts/measure_board_energy.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" push "$ROOT_DIR/scripts/measure_process_cpu.sh" "$ENERGY_REMOTE_DIR/" >/dev/null
    "$ADB" shell "chmod +x '$ENERGY_REMOTE_DIR/measure_board_energy.sh' '$ENERGY_REMOTE_DIR/measure_process_cpu.sh'"
}

write_remote_workload() {
    local local_file="$1"
    local remote_file="$2"
    local workload="$3"
    cat > "$local_file" <<EOF
#!/system/bin/sh
set -eu
$workload
EOF
    "$ADB" push "$local_file" "$remote_file" >/dev/null
    "$ADB" shell "chmod +x '$remote_file'"
}

run_direct() {
    local label="$1"
    local workload="$2"
    local repeat="$3"
    local safe run_id out_dir log summary local_script remote_script

    safe="$(safe_label "$label")"
    run_id="$(date +%Y%m%d_%H%M%S)_r${repeat}_${safe}_direct"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/direct_energy.log"
    summary="$out_dir/summary.env"
    local_script="$out_dir/workload.sh"
    remote_script="$ENERGY_REMOTE_DIR/${safe}.r${repeat}.direct.workload.sh"
    mkdir -p "$out_dir"
    write_remote_workload "$local_script" "$remote_script" "$workload"

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

    "$ADB" shell "cd '$ENERGY_REMOTE_DIR' && IDLE_POSITION='$DIRECT_IDLE_POSITION' IDLE_SECONDS='$DIRECT_IDLE_SECONDS' SAMPLE_INTERVAL='$DIRECT_SAMPLE_INTERVAL' sh ./measure_board_energy.sh '$label' sh '$remote_script'" > "$log" 2>&1

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

run_process() {
    local label="$1"
    local workload="$2"
    local repeat="$3"
    local safe run_id out_dir log summary local_script remote_script

    safe="$(safe_label "$label")"
    run_id="$(date +%Y%m%d_%H%M%S)_r${repeat}_${safe}_process"
    out_dir="$OUT_ROOT/$run_id"
    log="$out_dir/process_cpu.log"
    summary="$out_dir/summary.env"
    local_script="$out_dir/process_workload.sh"
    remote_script="$ENERGY_REMOTE_DIR/${safe}.r${repeat}.process.workload.sh"
    mkdir -p "$out_dir"
    write_remote_workload "$local_script" "$remote_script" "$workload"

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

csv_row_from_summary() {
    local repeat="$1"
    local level="$2"
    local backend="$3"
    local mode="$4"
    local summary="$5"
    local baseline_process="${6:-}"
    local result total elapsed us dec_s idle_w run_w delta_w delta_j uj tpw cpu_s cpu_pct cpu_ms reduction samples out_dir

    result="$(read_summary "$summary" result)"
    total="$(read_summary "$summary" total_decodes)"
    elapsed="$(read_summary "$summary" elapsed_ms)"
    us="$(read_summary "$summary" us_per_decode)"
    out_dir="$(read_summary "$summary" out_dir)"
    dec_s=""; idle_w=""; run_w=""; delta_w=""; delta_j=""; uj=""; tpw=""
    cpu_s=""; cpu_pct=""; cpu_ms=""; reduction=""; samples=""
    if [ "$mode" = "direct" ]; then
        dec_s="$(read_summary "$summary" decodes_per_s)"
        idle_w="$(read_summary "$summary" idle_avg_W)"
        run_w="$(read_summary "$summary" run_avg_W)"
        delta_w="$(read_summary "$summary" delta_W)"
        delta_j="$(read_summary "$summary" delta_energy_J)"
        uj="$(read_summary "$summary" uJ_per_decode)"
        tpw="$(read_summary "$summary" throughput_per_W)"
        samples="$(read_summary "$summary" run_samples)"
    else
        cpu_s="$(read_summary "$summary" process_cpu_s)"
        cpu_pct="$(read_summary "$summary" process_cpu_pct)"
        cpu_ms="$(read_summary "$summary" cpu_ms_per_decode)"
        samples="$(read_summary "$summary" process_samples)"
        if [ -n "$baseline_process" ]; then
            local base_cpu_ms
            base_cpu_ms="$(read_summary "$baseline_process" cpu_ms_per_decode)"
            reduction="$(awk -v base="${base_cpu_ms:-0}" -v cur="${cpu_ms:-0}" 'BEGIN {if (base > 0) printf "%.6f", 100.0 * (base - cur) / base}')"
        fi
    fi
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$repeat" "$level" "$backend" "$mode" "${result:-NA}" "${total:-}" "${elapsed:-}" "${us:-}" \
        "${dec_s:-}" "${idle_w:-}" "${run_w:-}" "${delta_w:-}" "${delta_j:-}" "${uj:-}" "${tpw:-}" \
        "${cpu_s:-}" "${cpu_pct:-}" "${cpu_ms:-}" "${reduction:-}" "${samples:-}" "$out_dir" >> "$SUMMARY_CSV"
    cat "$summary" >> "$RAW_FILE"
    printf '\n' >> "$RAW_FILE"
}

check_pass() {
    local summary="$1"
    local result
    result="$(read_summary "$summary" result)"
    [ "$result" = "PASS" ]
}

write_sanity_metric() {
    local level="$1"
    local backend="$2"
    local summary="$3"
    local metric="$4"
    local tolerance="$5"
    local measured reference diff status out_dir result
    measured="$(read_summary "$summary" "$metric")"
    reference="$(baseline_value "$level" "$backend" "$metric")"
    out_dir="$(read_summary "$summary" out_dir)"
    result="$(read_summary "$summary" result)"
    diff="$(awk -v ref="$reference" -v got="${measured:-0}" 'BEGIN {if (ref > 0) printf "%.3f", 100.0 * (got - ref) / ref; else print "0.000"}')"
    status="$(awk -v d="$diff" -v tol="$tolerance" -v r="$result" 'BEGIN {if (d < 0) d = -d; print (r == "PASS" && d <= tol) ? "PASS" : "FAIL"}')"
    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$level" "$backend" "${result:-NA}" "$metric" "$reference" "${measured:-}" "$diff" "$tolerance" "$status" "$out_dir" >> "$SANITY_CSV"
    [ "$status" = "PASS" ]
}

aggregate_results() {
    awk -F, '
    NR == 1 {next}
    $5 != "PASS" {next}
    $4 == "direct" {
        key=$2 SUBSEP $3
        add(key, "us_per_decode", $8)
        add(key, "decodes_per_s", $9)
        if ($12 > 0) {
            add(key, "delta_W", $12)
            add(key, "uJ_per_decode", $14)
            add(key, "throughput_per_W", $15)
        }
    }
    function add(key, metric, value, k) {
        if (value == "") return
        k=key SUBSEP metric
        n[k]++
        sum[k]+=value
        sumsq[k]+=value*value
    }
    END {
        print "level,backend,metric,n,mean,std"
        for (k in n) {
            split(k, p, SUBSEP)
            mean=sum[k]/n[k]
            std=0
            if (n[k] > 1) {
                var=(sumsq[k] - (sum[k]*sum[k]/n[k]))/(n[k]-1)
                if (var < 0 && var > -0.000000001) var=0
                std=sqrt(var)
            }
            printf "%s,%s,%s,%d,%.9f,%.9f\n", p[1], p[2], p[3], n[k], mean, std
        }
    }' "$SUMMARY_CSV" > "$AGG_DIRECT_CSV"

    awk -F, '
    NR == 1 {next}
    $5 != "PASS" {next}
    $4 == "process" {
        key=$2 SUBSEP $3
        add(key, "process_cpu_pct", $17)
        add(key, "cpu_ms_per_decode", $18)
        if ($19 != "") add(key, "reduction_vs_cpu_pct", $19)
    }
    function add(key, metric, value, k) {
        if (value == "") return
        k=key SUBSEP metric
        n[k]++
        sum[k]+=value
        sumsq[k]+=value*value
    }
    END {
        print "level,backend,metric,n,mean,std"
        for (k in n) {
            split(k, p, SUBSEP)
            mean=sum[k]/n[k]
            std=0
            if (n[k] > 1) {
                var=(sumsq[k] - (sum[k]*sum[k]/n[k]))/(n[k]-1)
                if (var < 0 && var > -0.000000001) var=0
                std=sqrt(var)
            }
            printf "%s,%s,%s,%d,%.9f,%.9f\n", p[1], p[2], p[3], n[k], mean, std
        }
    }' "$SUMMARY_CSV" > "$AGG_PROCESS_CSV"

    append_derived_direct
}

append_derived_direct() {
    awk -F, '
    NR == 1 {next}
    $5 != "PASS" || $4 != "direct" {next}
    {
        key=$1 SUBSEP $2
        us[key,$3]=$8
        if ($12 > 0) uj[key,$3]=$14
    }
    END {
        for (idx in us) {
            split(idx, p, SUBSEP)
            repeat=p[1]; level=p[2]; backend=p[3]
            levels[level]=1
            repeats[repeat SUBSEP level]=1
        }
        for (rl in repeats) {
            split(rl, p, SUBSEP)
            repeat=p[1]; level=p[2]
            cpu=repeat SUBSEP level SUBSEP "CPU scalar"
            npu=repeat SUBSEP level SUBSEP "NPU fastest non-CT"
            if (us[cpu] > 0 && us[npu] > 0) add(level SUBSEP "NPU fastest non-CT", "speedup_vs_cpu", us[cpu]/us[npu])
            if (uj[cpu] > 0 && uj[npu] > 0) add(level SUBSEP "NPU fastest non-CT", "energy_gain_vs_cpu", uj[cpu]/uj[npu])
        }
        for (k in n) {
            split(k, p, SUBSEP)
            mean=sum[k]/n[k]
            std=0
            if (n[k] > 1) {
                var=(sumsq[k] - (sum[k]*sum[k]/n[k]))/(n[k]-1)
                if (var < 0 && var > -0.000000001) var=0
                std=sqrt(var)
            }
            printf "%s,%s,%s,%d,%.9f,%.9f\n", p[1], p[2], p[3], n[k], mean, std
        }
    }
    function add(key, metric, value, k) {
        k=key SUBSEP metric
        n[k]++
        sum[k]+=value
        sumsq[k]+=value*value
    }' "$SUMMARY_CSV" >> "$AGG_DIRECT_CSV"
}

lookup_agg() {
    local file="$1"
    local level="$2"
    local backend="$3"
    local metric="$4"
    local field="$5"
    awk -F, -v l="$level" -v b="$backend" -v m="$metric" -v f="$field" '
        NR == 1 {
            for (i = 1; i <= NF; ++i) col[$i] = i
            next
        }
        $1 == l && $2 == b && $3 == m {print $col[f]; exit}
    ' "$file"
}

fmt_pm() {
    local mean="$1"
    local std="$2"
    local places="${3:-3}"
    awk -v m="${mean:-0}" -v s="${std:-0}" -v p="$places" 'BEGIN {
        fmt="%." p "f +/- %." p "f"
        printf fmt, m, s
    }'
}

fmt_num() {
    local value="$1"
    local places="${2:-3}"
    awk -v v="${value:-0}" -v p="$places" 'BEGIN {
        fmt="%." p "f"
        printf fmt, v
    }'
}

fmt_var() {
    local std="$1"
    local places="${2:-6}"
    awk -v s="${std:-0}" -v p="$places" 'BEGIN {
        fmt="%." p "f"
        printf fmt, s * s
    }'
}

write_markdown() {
    {
        echo
        echo "## Real-Device Mean/Std Rerun"
        echo
        echo "Status: complete"
        echo
        echo "Generated by:"
        echo
        echo '```sh'
        echo "REPEATS=$REPEATS RUN_DIRECT_ENERGY=$RUN_DIRECT_ENERGY RUN_QPROF_CONTEXT=0 scripts/measure_android_decode_stats.sh"
        echo '```'
        echo
        echo "Device path uses Windows ADB through WSL:"
        echo
        echo '```text'
        echo "$ADB"
        echo '```'
        echo
        echo "Profiler settings:"
        echo
        echo "- Direct energy source: Android power-supply voltage/current sampled by \`measure_board_energy.sh\`"
        echo "- \`DIRECT_SAMPLE_INTERVAL=$DIRECT_SAMPLE_INTERVAL\`"
        echo "- \`DIRECT_IDLE_SECONDS=$DIRECT_IDLE_SECONDS\`"
        echo "- \`DIRECT_IDLE_POSITION=$DIRECT_IDLE_POSITION\`"
        echo "- qprof context enabled: \`0\`"
        echo "- Process CPU source: per-thread \`/proc/<pid>/task/*/stat\` ticks sampled by \`measure_process_cpu.sh\`"
        echo "- \`PROCESS_CPU_SAMPLE_INTERVAL=$PROCESS_CPU_SAMPLE_INTERVAL\`"
        echo "- CPU target decodes: \`$CPU_TARGET_DECODES\`"
        echo "- NPU target decodes: \`${NPU_TARGET_DECODES:-default level-specific iters}\`"
        echo "- CPU path: scalar ARM64 baseline"
        echo "- NPU path: current non-worker \`labs/fastest\` FastRPC build"
        echo
        echo "## Sanity Check Against Pre-Worker Baseline"
        echo
        echo "| HQC | Backend | Metric | Reference | Measured | Diff % | Tolerance % | Status | Raw dir |"
        echo "| --- | --- | --- | ---: | ---: | ---: | ---: | --- | --- |"
        awk -F, 'NR > 1 {printf "| HQC-%s | %s | `%s` | %s | %s | %s | %s | %s | `%s` |\n", $1, $2, $4, $5, $6, $7, $8, $9, $10}' "$SANITY_CSV"
        echo
        echo "## Direct Energy Aggregate"
        echo
        echo "| HQC | Backend | n | us mean | us std | us var | dec/s mean | dec/s std | dec/s var | delta W mean | delta W std | delta W var | uJ mean | uJ std | uJ var | dec/s/W mean | dec/s/W std | dec/s/W var | speedup mean | speedup std | speedup var | energy gain mean | energy gain std | energy gain var |"
        echo "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        for level in $LEVELS; do
            for backend in "CPU scalar" "NPU fastest non-CT"; do
                local n us_m us_s ds_m ds_s dw_m dw_s uj_m uj_s tpw_m tpw_s sp_m sp_s eg_m eg_s
                n="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" us_per_decode n)"
                us_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" us_per_decode mean)"
                us_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" us_per_decode std)"
                ds_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" decodes_per_s mean)"
                ds_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" decodes_per_s std)"
                dw_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" delta_W mean)"
                dw_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" delta_W std)"
                uj_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" uJ_per_decode mean)"
                uj_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" uJ_per_decode std)"
                tpw_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" throughput_per_W mean)"
                tpw_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" throughput_per_W std)"
                sp_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" speedup_vs_cpu mean)"
                sp_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" speedup_vs_cpu std)"
                eg_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" energy_gain_vs_cpu mean)"
                eg_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" energy_gain_vs_cpu std)"
                [ -n "$n" ] || n=0
                [ "$backend" = "CPU scalar" ] && sp_m=1 && sp_s=0 && eg_m=1 && eg_s=0
                printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' \
                    "$level" "$backend" "$n" \
                    "$(fmt_num "$us_m" 3)" "$(fmt_num "$us_s" 3)" "$(fmt_var "$us_s" 9)" \
                    "$(fmt_num "$ds_m" 3)" "$(fmt_num "$ds_s" 3)" "$(fmt_var "$ds_s" 3)" \
                    "$(fmt_num "$dw_m" 9)" "$(fmt_num "$dw_s" 9)" "$(fmt_var "$dw_s" 9)" \
                    "$(fmt_num "$uj_m" 3)" "$(fmt_num "$uj_s" 3)" "$(fmt_var "$uj_s" 3)" \
                    "$(fmt_num "$tpw_m" 3)" "$(fmt_num "$tpw_s" 3)" "$(fmt_var "$tpw_s" 3)" \
                    "$(fmt_num "$sp_m" 2)" "$(fmt_num "$sp_s" 2)" "$(fmt_var "$sp_s" 4)" \
                    "$(fmt_num "$eg_m" 2)" "$(fmt_num "$eg_s" 2)" "$(fmt_var "$eg_s" 4)"
            done
        done
        echo
        echo "## Process CPU Aggregate"
        echo
        echo "| HQC | Backend | n | process CPU mean % | process CPU std | process CPU var | CPU ms/decode mean | CPU ms/decode std | CPU ms/decode var | CPU reduction mean % | CPU reduction std | CPU reduction var |"
        echo "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"
        for level in $LEVELS; do
            for backend in "CPU scalar" "NPU fastest non-CT"; do
                local n pct_m pct_s ms_m ms_s red_m red_s
                n="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" cpu_ms_per_decode n)"
                pct_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" process_cpu_pct mean)"
                pct_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" process_cpu_pct std)"
                ms_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" cpu_ms_per_decode mean)"
                ms_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" cpu_ms_per_decode std)"
                red_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" reduction_vs_cpu_pct mean)"
                red_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" reduction_vs_cpu_pct std)"
                [ -n "$n" ] || n=0
                [ "$backend" = "CPU scalar" ] && red_m=0 && red_s=0
                printf '| HQC-%s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' \
                    "$level" "$backend" "$n" \
                    "$(fmt_num "$pct_m" 3)" "$(fmt_num "$pct_s" 3)" "$(fmt_var "$pct_s" 6)" \
                    "$(fmt_num "$ms_m" 7)" "$(fmt_num "$ms_s" 7)" "$(fmt_var "$ms_s" 10)" \
                    "$(fmt_num "$red_m" 3)" "$(fmt_num "$red_s" 3)" "$(fmt_var "$red_s" 6)"
            done
        done
        echo
        echo "## Raw Files"
        echo
        echo "- Summary CSV: \`$SUMMARY_CSV\`"
        echo "- Direct aggregate CSV: \`$AGG_DIRECT_CSV\`"
        echo "- Process aggregate CSV: \`$AGG_PROCESS_CSV\`"
        echo "- Raw summary env list: \`$RAW_FILE\`"
    } >> "$RESULT_MD"
}

tex_pm() {
    local mean="$1"
    local std="$2"
    local places="${3:-3}"
    awk -v m="${mean:-0}" -v s="${std:-0}" -v p="$places" 'BEGIN {
        fmt="%." p "f $\\pm$ %." p "f"
        printf fmt, m, s
    }'
}

write_tex() {
    {
        echo "% Generated by scripts/measure_android_decode_stats.sh"
        echo "% Paste these rows into git/Benchmark.tex Real-Device Results."
        echo
        echo "% Direct energy rows: HQC & Backend & us/decode & uJ/decode & decodes/s/W & Speedup & Energy gain"
        for level in $LEVELS; do
            for backend in "CPU scalar" "NPU fastest non-CT"; do
                local us_m us_s uj_m uj_s tpw_m tpw_s sp_m sp_s eg_m eg_s tex_backend
                us_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" us_per_decode mean)"
                us_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" us_per_decode std)"
                uj_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" uJ_per_decode mean)"
                uj_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" uJ_per_decode std)"
                tpw_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" throughput_per_W mean)"
                tpw_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" throughput_per_W std)"
                sp_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" speedup_vs_cpu mean)"
                sp_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" speedup_vs_cpu std)"
                eg_m="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" energy_gain_vs_cpu mean)"
                eg_s="$(lookup_agg "$AGG_DIRECT_CSV" "$level" "$backend" energy_gain_vs_cpu std)"
                [ "$backend" = "CPU scalar" ] && sp_m=1 && sp_s=0 && eg_m=1 && eg_s=0
                tex_backend="$backend"
                [ "$backend" = "NPU fastest non-CT" ] && tex_backend="NPU-supported"
                printf 'HQC-%s & %s & %s & %s & %s & %s & %s \\\\\n' \
                    "$level" "$tex_backend" "$(tex_pm "$us_m" "$us_s" 3)" "$(tex_pm "$uj_m" "$uj_s" 3)" \
                    "$(tex_pm "$tpw_m" "$tpw_s" 3)" "$(tex_pm "$sp_m" "$sp_s" 2)" "$(tex_pm "$eg_m" "$eg_s" 2)"
            done
        done
        echo
        echo "% Process CPU rows: HQC & Backend & process CPU % & CPU ms/decode & CPU reduction"
        for level in $LEVELS; do
            for backend in "CPU scalar" "NPU fastest non-CT"; do
                local pct_m pct_s ms_m ms_s red_m red_s tex_backend
                pct_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" process_cpu_pct mean)"
                pct_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" process_cpu_pct std)"
                ms_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" cpu_ms_per_decode mean)"
                ms_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" cpu_ms_per_decode std)"
                red_m="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" reduction_vs_cpu_pct mean)"
                red_s="$(lookup_agg "$AGG_PROCESS_CSV" "$level" "$backend" reduction_vs_cpu_pct std)"
                [ "$backend" = "CPU scalar" ] && red_m=0 && red_s=0
                tex_backend="$backend"
                [ "$backend" = "NPU fastest non-CT" ] && tex_backend="NPU-supported"
                printf 'HQC-%s & %s & %s & %s & %s\\%% \\\\\n' \
                    "$level" "$tex_backend" "$(tex_pm "$pct_m" "$pct_s" 3)" "$(tex_pm "$ms_m" "$ms_s" 7)" "$(tex_pm "$red_m" "$red_s" 3)"
            done
        done
    } > "$RESULT_TEX"
}

finalize_outputs() {
    aggregate_results
    write_markdown
    write_tex
}

prepare_level() {
    local level="$1"
    local cpu_iters="$2"
    local npu_iters="$3"
    if [ "$SKIP_BUILD" = "1" ]; then
        CPU_DIR="/data/local/tmp/QDC_files/hqc_whole_stats/hqc${level}_cpu_scalar"
        NPU_DIR="/data/local/tmp/QDC_files/hqc_whole_stats/hqc${level}_npu_fastest_nonct"
    else
        build_cpu "$level" "$cpu_iters"
        CPU_DIR="$(deploy_cpu "$level")"
        build_npu "$level" "$npu_iters"
        NPU_DIR="$(deploy_npu "$level")"
    fi
}

run_sanity() {
    local failures=0
    echo "[stats] running sanity check against pre-worker baseline"
    for level in $LEVELS; do
        local cpu_iters npu_iters cpu_workload npu_workload cpu_summary npu_summary
        cpu_iters="$(iters_for CPU "$level")"
        npu_iters="$(iters_for NPU "$level")"
        prepare_level "$level" "$cpu_iters" "$npu_iters"
        cpu_workload="cd $CPU_DIR && chmod +x hqc${level}_decode_bench_arm64_android && ./hqc${level}_decode_bench_arm64_android"
        npu_workload="cd $NPU_DIR && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host $npu_iters"
        cpu_summary="$(run_direct "sanity_hqc${level}_cpu_scalar" "$cpu_workload" 0)"
        npu_summary="$(run_direct "sanity_hqc${level}_npu_fastest_nonct" "$npu_workload" 0)"
        write_sanity_metric "$level" "CPU scalar" "$cpu_summary" us_per_decode "$CPU_LATENCY_TOLERANCE_PCT" || failures=$((failures + 1))
        write_sanity_metric "$level" "NPU fastest non-CT" "$npu_summary" us_per_decode "$NPU_LATENCY_TOLERANCE_PCT" || failures=$((failures + 1))
        write_sanity_metric "$level" "NPU fastest non-CT" "$npu_summary" uJ_per_decode "$NPU_ENERGY_TOLERANCE_PCT" || failures=$((failures + 1))
    done
    if [ "$failures" -ne 0 ]; then
        echo "ERROR: sanity check failed with $failures failing metric(s); see $SANITY_CSV" >&2
        return 1
    fi
}

run_repeats() {
    for repeat in $(seq 1 "$REPEATS"); do
        echo "[stats] === repeat $repeat/$REPEATS ==="
        for level in $LEVELS; do
            local cpu_iters npu_iters cpu_workload cpu_process_workload npu_workload npu_process_workload
            local cpu_direct cpu_process npu_direct npu_process
            cpu_iters="$(iters_for CPU "$level")"
            npu_iters="$(iters_for NPU "$level")"
            prepare_level "$level" "$cpu_iters" "$npu_iters"
            cpu_workload="cd $CPU_DIR && chmod +x hqc${level}_decode_bench_arm64_android && ./hqc${level}_decode_bench_arm64_android"
            cpu_process_workload="cd $CPU_DIR && chmod +x hqc${level}_decode_bench_arm64_android && exec ./hqc${level}_decode_bench_arm64_android"
            npu_workload="cd $NPU_DIR && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && ./hqc_host $npu_iters"
            npu_process_workload="cd $NPU_DIR && chmod +x hqc_host && export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && exec ./hqc_host $npu_iters"

            cpu_direct="$(run_direct "hqc${level}_cpu_scalar" "$cpu_workload" "$repeat")"
            check_pass "$cpu_direct"
            csv_row_from_summary "$repeat" "$level" "CPU scalar" direct "$cpu_direct"

            cpu_process="$(run_process "hqc${level}_cpu_scalar" "$cpu_process_workload" "$repeat")"
            check_pass "$cpu_process"
            csv_row_from_summary "$repeat" "$level" "CPU scalar" process "$cpu_process" "$cpu_process"

            npu_direct="$(run_direct "hqc${level}_npu_fastest_nonct" "$npu_workload" "$repeat")"
            check_pass "$npu_direct"
            csv_row_from_summary "$repeat" "$level" "NPU fastest non-CT" direct "$npu_direct"

            npu_process="$(run_process "hqc${level}_npu_fastest_nonct" "$npu_process_workload" "$repeat")"
            check_pass "$npu_process"
            csv_row_from_summary "$repeat" "$level" "NPU fastest non-CT" process "$npu_process" "$cpu_process"
        done
    done
}

echo "[stats] ADB=$ADB"
echo "[stats] OUT_ROOT=$OUT_ROOT"
echo "[stats] RESULT_MD=$RESULT_MD"
echo "[stats] RESULT_TEX=$RESULT_TEX"
echo "[stats] LEVELS=$LEVELS REPEATS=$REPEATS"
deploy_measure_scripts

if [ "$RUN_SANITY" = "1" ]; then
    run_sanity
fi

if [ "$SANITY_ONLY" = "1" ]; then
    echo "[stats] sanity-only complete; see $SANITY_CSV"
    exit 0
fi

run_repeats
finalize_outputs
echo "[stats] wrote $RESULT_MD"
echo "[stats] wrote $RESULT_TEX"
