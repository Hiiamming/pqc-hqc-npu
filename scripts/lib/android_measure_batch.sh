#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/batch_size/batch_size_$RUN_ID}"
RESULT_MD="${RESULT_MD:-$ROOT_DIR/result.md}"
REMOTE_ROOT="${REMOTE_ROOT:-/data/local/tmp/QDC_files/hqc_batch_size}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
LEVELS="${LEVELS:-128 192 256}"
REPEATS="${REPEATS:-5}"
BATCH_SIZES="${BATCH_SIZES:-1 2 4 8 16 32 64 128 256}"
BATCH_TARGET_DECODES="${BATCH_TARGET_DECODES:-32768}"
BUFFER_ALLOC="${BUFFER_ALLOC:-rpcmem-cached}"
BUFFER_DSP_MODE="${BUFFER_DSP_MODE:-direct}"
SKIP_BUILD="${SKIP_BUILD:-0}"

source "$ROOT_DIR/scripts/lib/android_device_common.sh"
android_device_require_adb
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required to generate the batch-size SVG chart" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

SUMMARY_CSV="$OUT_ROOT/summary.csv"
AGG_CSV="$OUT_ROOT/aggregate.csv"
CHART_SVG="$OUT_ROOT/batch_size_us_per_decode.svg"
CHART_PNG="$OUT_ROOT/batch_size_us_per_decode.png"
RAW_FILE="$OUT_ROOT/raw_logs.txt"

cat > "$SUMMARY_CSV" <<'EOF'
repeat,level,batch_size,iters,rpc_calls,total_decodes,result,mode_status,elapsed_ms,us_per_decode,out_dir
EOF
: > "$RAW_FILE"

positive_int() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) [ "$1" -gt 0 ] ;;
    esac
}

for value in $LEVELS $REPEATS $BATCH_SIZES $BATCH_TARGET_DECODES; do
    if ! positive_int "$value"; then
        echo "ERROR: expected positive integer, got '$value'" >&2
        exit 2
    fi
done

rpc_calls_for_batch() {
    local batch="$1"
    awk -v target="$BATCH_TARGET_DECODES" -v batch="$batch" 'BEGIN {
        calls = int((target + batch - 1) / batch);
        if (calls < 1) calls = 1;
        print calls;
    }'
}

csv_row_from_log() {
    local repeat="$1"
    local level="$2"
    local batch="$3"
    local iters="$4"
    local rpc_calls="$5"
    local log="$6"
    local row_dir="$7"
    local total result mode_status elapsed us

    total="$(android_device_extract_field "$log" total_decodes)"
    result="$(android_device_extract_field "$log" result)"
    mode_status="$(android_device_extract_field "$log" mode_status)"
    elapsed="$(android_device_extract_field "$log" elapsed_ms)"
    us="$(android_device_extract_field "$log" us_per_decode)"

    printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$repeat" "$level" "$batch" "$iters" "$rpc_calls" "${total:-}" \
        "${result:-NA}" "${mode_status:-}" "${elapsed:-}" "${us:-}" "$row_dir" >> "$SUMMARY_CSV"

    printf '%s\n' "$log" >> "$RAW_FILE"

    if [ "${result:-}" != "PASS" ]; then
        echo "ERROR: batch measurement failed: level=$level batch=$batch repeat=$repeat result=${result:-NA}; see $log" >&2
        return 1
    fi
}

aggregate_results() {
    local tmp="$OUT_ROOT/aggregate.body.csv"
    awk -F, '
    NR == 1 {next}
    $7 != "PASS" {next}
    {
        key=$2 SUBSEP $3
        n[key]++
        sum_us[key]+=$10
        sumsq_us[key]+=$10*$10
        sum_total[key]+=$6
    }
    END {
        for (key in n) {
            split(key, p, SUBSEP)
            mean=sum_us[key]/n[key]
            std=0
            if (n[key] > 1) {
                var=(sumsq_us[key] - (sum_us[key]*sum_us[key]/n[key]))/(n[key]-1)
                if (var < 0 && var > -0.000000001) var=0
                std=sqrt(var)
            }
            printf "%s,%s,%d,%.9f,%.9f,%.3f\n", p[1], p[2], n[key], mean, std, sum_total[key]/n[key]
        }
    }' "$SUMMARY_CSV" > "$tmp"

    {
        echo "level,batch_size,n,us_per_decode_mean,us_per_decode_std,total_decodes_mean"
        sort -t, -k1,1n -k2,2n "$tmp"
    } > "$AGG_CSV"
}

write_svg_chart() {
    python3 - "$AGG_CSV" "$CHART_SVG" <<'PY'
import csv
import html
import math
import sys

agg_csv, out_svg = sys.argv[1], sys.argv[2]
rows = []
with open(agg_csv, newline="") as f:
    for row in csv.DictReader(f):
        rows.append({
            "level": row["level"],
            "batch": int(row["batch_size"]),
            "us": float(row["us_per_decode_mean"]),
            "std": float(row["us_per_decode_std"]),
        })

if not rows:
    raise SystemExit("no aggregate rows to plot")

levels = sorted({r["level"] for r in rows}, key=int)
batches = sorted({r["batch"] for r in rows})
y_values = [r["us"] for r in rows]
min_log = math.log2(min(batches))
max_log = math.log2(max(batches))
if min_log == max_log:
    max_log = min_log + 1.0

width, height = 960, 560
left, right, top, bottom = 86, 28, 44, 78
plot_w = width - left - right
plot_h = height - top - bottom
y_max = max(y_values) * 1.08
y_min = 0.0
colors = {
    "128": "#2563eb",
    "192": "#059669",
    "256": "#dc2626",
}

def x_pos(batch):
    return left + (math.log2(batch) - min_log) / (max_log - min_log) * plot_w

def y_pos(us):
    return top + (y_max - us) / (y_max - y_min) * plot_h

parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
    '<rect width="100%" height="100%" fill="#ffffff"/>',
    '<style>text{font-family:Arial,Helvetica,sans-serif;fill:#111827} .muted{fill:#6b7280} .grid{stroke:#e5e7eb;stroke-width:1} .axis{stroke:#374151;stroke-width:1.4}</style>',
    f'<text x="{left}" y="27" font-size="18" font-weight="700">FastRPC Batch Size vs Decode Time</text>',
    f'<text x="{left}" y="49" font-size="12" class="muted">buffer-bench rpcmem-cached direct, real-device wall-clock mean</text>',
]

ticks = 5
for i in range(ticks + 1):
    value = y_min + (y_max - y_min) * i / ticks
    y = y_pos(value)
    parts.append(f'<line class="grid" x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}"/>')
    parts.append(f'<text x="{left - 10}" y="{y + 4:.2f}" font-size="11" text-anchor="end" class="muted">{value:.1f}</text>')

for batch in batches:
    x = x_pos(batch)
    parts.append(f'<line class="grid" x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}"/>')
    parts.append(f'<text x="{x:.2f}" y="{top + plot_h + 23}" font-size="11" text-anchor="middle" class="muted">{batch}</text>')

parts.append(f'<line class="axis" x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}"/>')
parts.append(f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}"/>')
parts.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 28}" font-size="13" text-anchor="middle">Batch size (codewords per FastRPC call, log2 spacing)</text>')
parts.append(f'<text transform="translate(22 {top + plot_h / 2:.2f}) rotate(-90)" font-size="13" text-anchor="middle">us/decode</text>')

for level in levels:
    series = sorted((r for r in rows if r["level"] == level), key=lambda r: r["batch"])
    points = " ".join(f'{x_pos(r["batch"]):.2f},{y_pos(r["us"]):.2f}' for r in series)
    color = colors.get(level, "#4b5563")
    parts.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="2.6" stroke-linejoin="round" stroke-linecap="round"/>')
    for r in series:
        x, y = x_pos(r["batch"]), y_pos(r["us"])
        parts.append(f'<circle cx="{x:.2f}" cy="{y:.2f}" r="4" fill="{color}"><title>HQC-{html.escape(level)} batch {r["batch"]}: {r["us"]:.3f} us/decode</title></circle>')

legend_x = left + plot_w - 160
legend_y = top + 16
for i, level in enumerate(levels):
    y = legend_y + i * 24
    color = colors.get(level, "#4b5563")
    parts.append(f'<line x1="{legend_x}" y1="{y}" x2="{legend_x + 28}" y2="{y}" stroke="{color}" stroke-width="2.6"/>')
    parts.append(f'<circle cx="{legend_x + 14}" cy="{y}" r="4" fill="{color}"/>')
    parts.append(f'<text x="{legend_x + 38}" y="{y + 4}" font-size="12">HQC-{html.escape(level)}</text>')

parts.append("</svg>")
with open(out_svg, "w", encoding="utf-8") as f:
    f.write("\n".join(parts))
PY
}

write_png_chart_if_possible() {
    if ! command -v python3 >/dev/null 2>&1; then
        return
    fi
    python3 - "$AGG_CSV" "$CHART_PNG" >/dev/null 2>&1 <<'PY' || true
import csv
import sys

try:
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
except Exception:
    raise SystemExit(0)

agg_csv, out_png = sys.argv[1], sys.argv[2]
rows = []
with open(agg_csv, newline="") as f:
    rows = list(csv.DictReader(f))
if not rows:
    raise SystemExit(0)

levels = sorted({r["level"] for r in rows}, key=int)
for level in levels:
    series = sorted((r for r in rows if r["level"] == level), key=lambda r: int(r["batch_size"]))
    plt.plot([int(r["batch_size"]) for r in series],
             [float(r["us_per_decode_mean"]) for r in series],
             marker="o",
             label=f"HQC-{level}")
plt.xscale("log", base=2)
plt.xlabel("Batch size (codewords per FastRPC call)")
plt.ylabel("us/decode")
plt.title("FastRPC Batch Size vs Decode Time")
plt.grid(True, which="both", color="#e5e7eb")
plt.legend()
plt.tight_layout()
plt.savefig(out_png, dpi=160)
PY
}

append_markdown() {
    {
        echo
        echo "## Real-Device Batch-Size Sweep"
        echo
        echo "Status: complete"
        echo
        echo "Generated by:"
        echo
        echo '```sh'
        echo "scripts/measure_android.sh --suite batch --repeats $REPEATS --target-decodes $BATCH_TARGET_DECODES"
        echo '```'
        echo
        echo "Measurement setup:"
        echo
        echo "- ADB: \`$ADB\`"
        echo "- Backend: \`labs/fastest\` FastRPC cDSP fastest path"
        echo "- Allocation: \`$BUFFER_ALLOC\`"
        echo "- DSP buffer mode: \`$BUFFER_DSP_MODE\`"
        echo "- Batch sizes: \`$BATCH_SIZES\`"
        echo "- Target decodes per point: \`$BATCH_TARGET_DECODES\`"
        echo "- Iterations per RPC: \`1\`"
        echo "- qprof context: \`0\`"
        echo
        echo "Chart:"
        echo
        echo "- SVG: \`$CHART_SVG\`"
        if [ -f "$CHART_PNG" ]; then
            echo "- PNG: \`$CHART_PNG\`"
        fi
        echo
        echo "| HQC | Batch size | n | us/decode mean | us/decode std | total decodes mean |"
        echo "| --- | ---: | ---: | ---: | ---: | ---: |"
        awk -F, 'NR > 1 {printf "| HQC-%s | %s | %s | %.3f | %.3f | %.0f |\n", $1, $2, $3, $4, $5, $6}' "$AGG_CSV"
        echo
        echo "Raw files:"
        echo
        echo "- Summary CSV: \`$SUMMARY_CSV\`"
        echo "- Aggregate CSV: \`$AGG_CSV\`"
        echo "- Raw log list: \`$RAW_FILE\`"
    } >> "$RESULT_MD"
}

echo "[batch] ADB=$ADB"
echo "[batch] OUT_ROOT=$OUT_ROOT"
echo "[batch] RESULT_MD=$RESULT_MD"
echo "[batch] LEVELS=$LEVELS REPEATS=$REPEATS"
echo "[batch] BATCH_SIZES=$BATCH_SIZES TARGET_DECODES=$BATCH_TARGET_DECODES"
echo "[batch] BUFFER_ALLOC=$BUFFER_ALLOC BUFFER_DSP_MODE=$BUFFER_DSP_MODE"

for level in $LEVELS; do
    label="hqc${level}"
    level_dir="$OUT_ROOT/$label"
    device_dir="$REMOTE_ROOT/$label"
    mkdir -p "$level_dir"

    if [ "$SKIP_BUILD" != "1" ]; then
        android_device_build_fastest "$level" 1
    fi

    android_device_deploy_fastrpc "$device_dir" >/dev/null

    {
        echo "level=$level"
        echo "repeats=$REPEATS"
        echo "batch_sizes=$BATCH_SIZES"
        echo "target_decodes=$BATCH_TARGET_DECODES"
        echo "buffer_alloc=$BUFFER_ALLOC"
        echo "buffer_dsp_mode=$BUFFER_DSP_MODE"
        echo "device_dir=$device_dir"
    } > "$level_dir/run.info"

    for repeat in $(seq 1 "$REPEATS"); do
        repeat_dir="$level_dir/run${repeat}"
        mkdir -p "$repeat_dir"

        for batch in $BATCH_SIZES; do
            iters=1
            rpc_calls="$(rpc_calls_for_batch "$batch")"
            log="$repeat_dir/batch_${batch}.log"
            echo "=== $label run $repeat/$REPEATS buffer-bench batch=$batch iters=$iters rpc_calls=$rpc_calls ==="
            android_device_run_fastrpc_remote "$device_dir" "$log" \
                buffer-bench "$BUFFER_ALLOC" "$BUFFER_DSP_MODE" "$iters" "$batch" "$rpc_calls"
            csv_row_from_log "$repeat" "$level" "$batch" "$iters" "$rpc_calls" "$log" "$repeat_dir"
        done
    done
done

aggregate_results
write_svg_chart
write_png_chart_if_possible
append_markdown

echo "[batch] wrote $SUMMARY_CSV"
echo "[batch] wrote $AGG_CSV"
echo "[batch] wrote $CHART_SVG"
if [ -f "$CHART_PNG" ]; then
    echo "[batch] wrote $CHART_PNG"
fi
echo "[batch] appended $RESULT_MD"
