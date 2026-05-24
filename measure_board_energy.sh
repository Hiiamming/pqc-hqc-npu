#!/bin/sh
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: $0 LABEL COMMAND [ARGS...]" >&2
    exit 2
fi

label="$1"
shift

default_supply="/sys/class/power_supply/battery"
if [ ! -f "$default_supply/voltage_now" ] || [ ! -f "$default_supply/current_now" ]; then
    default_supply="/sys/class/power_supply/qcom-battmgr-bat"
fi
vpath="${VOLTAGE_PATH:-$default_supply/voltage_now}"
ipath="${CURRENT_PATH:-$default_supply/current_now}"
interval="${SAMPLE_INTERVAL:-0.1}"
idle_position="${IDLE_POSITION:-before}"
tmp_dir="${ENERGY_TMP_DIR:-/data/local/tmp}"
mkdir -p "$tmp_dir"
out="$tmp_dir/${label}.bench.out"
run_samples="$tmp_dir/${label}.run.samples"
idle_samples="$tmp_dir/${label}.idle.samples"

uptime_s() {
    awk '{print $1}' /proc/uptime
}

sample_once() {
    v=$(cat "$vpath")
    i=$(cat "$ipath")
    awk -v v="$v" -v i="$i" 'BEGIN {
        if (i < 0) i = -i;
        printf "%.9f\n", (v * i) / 1000000000000.0;
    }'
}

avg_file() {
    awk '{sum += $1; n += 1} END {if (n) printf "%.9f\n", sum / n; else print "0"}' "$1"
}

echo "[energy] label=$label"
echo "[energy] voltage_path=$vpath"
echo "[energy] current_path=$ipath"
echo "[energy] sample_interval_s=$interval"
echo "[energy] idle_position=$idle_position"
echo "[energy] command=$*"

rm -f "$out" "$run_samples" "$idle_samples"

sample_idle_for() {
    idle_start=$(uptime_s)
    idle_end_target=$(awk -v s="$idle_start" -v e="$1" 'BEGIN {printf "%.6f\n", s + e}')
    while awk -v now="$(uptime_s)" -v target="$idle_end_target" 'BEGIN {exit !(now < target)}'; do
        sample_once >> "$idle_samples"
        sleep "$interval"
    done
}

run_workload() {
    start=$(uptime_s)
    "$@" > "$out" 2>&1 &
    pid=$!

    while kill -0 "$pid" 2>/dev/null; do
        sample_once >> "$run_samples"
        sleep "$interval"
    done
    set +e
    wait "$pid"
    rc=$?
    set -e
    end=$(uptime_s)
    elapsed=$(awk -v a="$start" -v b="$end" 'BEGIN {printf "%.6f\n", b - a}')
}

case "$idle_position" in
    before)
        estimate="${IDLE_SECONDS:-10}"
        sample_idle_for "$estimate"
        run_workload "$@"
        ;;
    after)
        run_workload "$@"
        sample_idle_for "$elapsed"
        ;;
    both)
        estimate="${IDLE_SECONDS:-10}"
        sample_idle_for "$estimate"
        run_workload "$@"
        sample_idle_for "$elapsed"
        ;;
    *)
        echo "ERROR: IDLE_POSITION must be before, after, or both" >&2
        exit 2
        ;;
esac

if [ ! -s "$idle_samples" ]; then
    sample_idle_for "$elapsed"
fi

while [ "$(wc -l < "$idle_samples" | tr -d ' ')" -lt 1 ]; do
    sample_once >> "$idle_samples"
done

run_avg=$(avg_file "$run_samples")
idle_avg=$(avg_file "$idle_samples")
run_samples_n=$(wc -l < "$run_samples" | tr -d ' ')
idle_samples_n=$(wc -l < "$idle_samples" | tr -d ' ')

total_decodes=$(sed -n 's/.*total_decodes=\([0-9][0-9]*\).*/\1/p' "$out" | tail -n 1)
us_per_decode=$(sed -n 's/.*us_per_decode=\([0-9.][0-9.]*\).*/\1/p' "$out" | tail -n 1)
result=$(sed -n 's/.*result=\([A-Z][A-Z]*\).*/\1/p' "$out" | tail -n 1)

if [ -z "$total_decodes" ]; then
    total_decodes=0
fi

awk -v label="$label" \
    -v rc="$rc" \
    -v result="$result" \
    -v elapsed="$elapsed" \
    -v total_decodes="$total_decodes" \
    -v us_per_decode="$us_per_decode" \
    -v run_avg="$run_avg" \
    -v idle_avg="$idle_avg" \
    -v run_samples="$run_samples_n" \
    -v idle_samples="$idle_samples_n" \
    'BEGIN {
        delta = run_avg - idle_avg;
        energy = delta * elapsed;
        if (total_decodes > 0) {
            uj = energy * 1000000.0 / total_decodes;
        } else {
            uj = 0;
        }
        printf "[energy-result] label=%s rc=%s result=%s elapsed_s=%.6f total_decodes=%d us_per_decode=%s run_avg_W=%.9f idle_avg_W=%.9f delta_W=%.9f delta_energy_J=%.9f uJ_per_decode=%.3f run_samples=%d idle_samples=%d\n",
               label, rc, result, elapsed, total_decodes, us_per_decode,
               run_avg, idle_avg, delta, energy, uj, run_samples, idle_samples;
    }'

cat "$out"
exit "$rc"
