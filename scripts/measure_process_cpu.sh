#!/system/bin/sh
set -eu

if [ "$#" -lt 2 ]; then
    echo "usage: $0 LABEL COMMAND [ARGS...]" >&2
    exit 2
fi

label="$1"
shift

interval="${PROCESS_CPU_SAMPLE_INTERVAL:-0.02}"
tmp_dir="${PROCESS_CPU_TMP_DIR:-/data/local/tmp}"
mkdir -p "$tmp_dir"
out="$tmp_dir/${label}.process_cpu.out"
samples="$tmp_dir/${label}.process_cpu.samples"

uptime_s() {
    awk '{print $1}' /proc/uptime
}

clk_tck() {
    getconf CLK_TCK 2>/dev/null || echo 100
}

thread_ticks_file() {
    pid="$1"
    total=0
    state=""
    for stat in /proc/"$pid"/task/*/stat; do
        [ -f "$stat" ] || continue
        line=$(cat "$stat" 2>/dev/null || true)
        [ -n "$line" ] || continue
        rest=${line##*) }
        set -- $rest
        [ "$#" -ge 13 ] || continue
        [ -z "$state" ] && state="$1"
        total=$((total + ${12} + ${13}))
    done
    printf '%s %s\n' "${state:-gone}" "$total"
}

extract_field() {
    key="$1"
    sed -n "s/.*$key=\\([^ ]*\\).*/\\1/p" "$out" | tr -d '\r' | tail -n 1
}

echo "[process-cpu] label=$label"
echo "[process-cpu] sample_interval_s=$interval"
echo "[process-cpu] command=$*"

rm -f "$out" "$samples"

hz=$(clk_tck)
start=$(uptime_s)
"$@" > "$out" 2>&1 &
pid=$!

first_ticks=""
last_ticks=""
sample_count=0

while :; do
    read state ticks <<EOF_TICKS
$(thread_ticks_file "$pid")
EOF_TICKS
    now=$(uptime_s)
    if [ "$state" != "gone" ]; then
        if [ -z "$first_ticks" ]; then
            first_ticks="$ticks"
        fi
        last_ticks="$ticks"
        sample_count=$((sample_count + 1))
        printf '%s %s %s\n' "$now" "$state" "$ticks" >> "$samples"
    fi
    [ "$state" = "gone" ] && break
    [ "$state" = "Z" ] && break
    sleep "$interval"
done

set +e
wait "$pid"
rc=$?
set -e
end=$(uptime_s)

elapsed=$(awk -v a="$start" -v b="$end" 'BEGIN {printf "%.6f", b - a}')
first_ticks="${first_ticks:-0}"
last_ticks="${last_ticks:-$first_ticks}"
delta_ticks=$((last_ticks - first_ticks))
if [ "$delta_ticks" -lt 0 ]; then
    delta_ticks=0
fi

total_decodes="$(extract_field total_decodes)"
us_per_decode="$(extract_field us_per_decode)"
result="$(extract_field result)"
elapsed_ms="$(extract_field elapsed_ms)"
if [ -z "$total_decodes" ]; then
    total_decodes=0
fi

awk -v label="$label" \
    -v rc="$rc" \
    -v result="$result" \
    -v elapsed="$elapsed" \
    -v elapsed_ms="$elapsed_ms" \
    -v total_decodes="$total_decodes" \
    -v us_per_decode="$us_per_decode" \
    -v hz="$hz" \
    -v delta_ticks="$delta_ticks" \
    -v samples="$sample_count" \
    'BEGIN {
        cpu_s = delta_ticks / hz;
        cpu_pct = (elapsed > 0) ? (100.0 * cpu_s / elapsed) : 0;
        cpu_ms_per_decode = (total_decodes > 0) ? (1000.0 * cpu_s / total_decodes) : 0;
        printf "[process-cpu-result] label=%s rc=%s result=%s elapsed_s=%.6f elapsed_ms=%s total_decodes=%d us_per_decode=%s process_cpu_s=%.9f process_cpu_pct=%.6f cpu_ms_per_decode=%.9f cpu_ticks=%d clk_tck=%d samples=%d\n",
               label, rc, result, elapsed, elapsed_ms, total_decodes, us_per_decode,
               cpu_s, cpu_pct, cpu_ms_per_decode, delta_ticks, hz, samples;
    }'

cat "$out"
exit "$rc"
