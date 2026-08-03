#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
ADB="${ADB:-/mnt/c/Temp/ADB/platform-tools/adb.exe}"
RUN_ID="${RUN_ID:-$(date +%Y%m%d_%H%M%S)}"
OUT_ROOT="${OUT_ROOT:-$ROOT_DIR/results/paper_batch_size/paper_batch_size_$RUN_ID}"
RESULT_MD="${RESULT_MD:-$ROOT_DIR/result.md}"
REMOTE_ROOT="${REMOTE_ROOT:-/data/local/tmp/QDC_files/hqc_paper_batch_size}"
HEXAGON_ARCH="${HEXAGON_ARCH:-v73}"
LEVELS="${LEVELS:-128 192 256}"
REPEATS="${REPEATS:-5}"
PAPER_BATCH_SIZES="${PAPER_BATCH_SIZES:-1 2 4 8 16 32 64 128 256 512 1024 2048 4096 8192 16384 32768}"
SKIP_BUILD="${SKIP_BUILD:-0}"

source "$ROOT_DIR/scripts/lib/android_device_common.sh"
android_device_require_adb
if ! command -v python3 >/dev/null 2>&1; then
    echo "ERROR: python3 is required to generate the paper-batch SVG chart" >&2
    exit 1
fi

mkdir -p "$OUT_ROOT"

SUMMARY_CSV="$OUT_ROOT/summary.csv"
AGG_CSV="$OUT_ROOT/aggregate.csv"
CHART_SVG="$OUT_ROOT/paper_batch_size_us_per_decode.svg"
RAW_FILE="$OUT_ROOT/raw_logs.txt"

cat > "$SUMMARY_CSV" <<'EOF'
repeat,level,batch_size,total_decodes,result,elapsed_ms,us_per_decode,out_dir
EOF
: > "$RAW_FILE"

positive_int() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) [ "$1" -gt 0 ] ;;
    esac
}

for value in $LEVELS $REPEATS $PAPER_BATCH_SIZES; do
    if ! positive_int "$value"; then
        echo "ERROR: expected positive integer, got '$value'" >&2
        exit 2
    fi
done

csv_row_from_log() {
    local repeat="$1"
    local level="$2"
    local batch="$3"
    local log="$4"
    local row_dir="$5"
    local total result elapsed us

    total="$(android_device_extract_field "$log" total_decodes)"
    result="$(android_device_extract_field "$log" result)"
    elapsed="$(android_device_extract_field "$log" elapsed_ms)"
    us="$(android_device_extract_field "$log" us_per_decode)"

    printf '%s,%s,%s,%s,%s,%s,%s,%s\n' \
        "$repeat" "$level" "$batch" "${total:-}" "${result:-NA}" \
        "${elapsed:-}" "${us:-}" "$row_dir" >> "$SUMMARY_CSV"

    printf '%s\n' "$log" >> "$RAW_FILE"

    if [ "${result:-}" != "PASS" ]; then
        echo "ERROR: paper-batch measurement failed: level=$level batch=$batch repeat=$repeat result=${result:-NA}; see $log" >&2
        return 1
    fi
}

aggregate_results() {
    local tmp="$OUT_ROOT/aggregate.body.csv"
    awk -F, '
    NR == 1 {next}
    $5 != "PASS" {next}
    {
        key=$2 SUBSEP $3
        n[key]++
        sum_us[key]+=$7
        sumsq_us[key]+=$7*$7
        sum_total[key]+=$4
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
colors = {"128": "#2563eb", "192": "#059669", "256": "#dc2626"}

def x_pos(batch):
    return left + (math.log2(batch) - min_log) / (max_log - min_log) * plot_w

def y_pos(us):
    return top + (y_max - us) / y_max * plot_h

parts = [
    f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
    '<rect width="100%" height="100%" fill="#ffffff"/>',
    '<style>text{font-family:Arial,Helvetica,sans-serif;fill:#111827}.muted{fill:#6b7280}.grid{stroke:#e5e7eb;stroke-width:1}.axis{stroke:#374151;stroke-width:1.4}</style>',
    f'<text x="{left}" y="27" font-size="18" font-weight="700">Paper-Style Batch Size vs Decode Time</text>',
    f'<text x="{left}" y="49" font-size="12" class="muted">one FastRPC call per point; fixture data stays inside DSP binary</text>',
]

for i in range(6):
    value = y_max * i / 5
    y = y_pos(value)
    parts.append(f'<line class="grid" x1="{left}" y1="{y:.2f}" x2="{left + plot_w}" y2="{y:.2f}"/>')
    parts.append(f'<text x="{left - 10}" y="{y + 4:.2f}" font-size="11" text-anchor="end" class="muted">{value:.1f}</text>')

for batch in batches:
    x = x_pos(batch)
    parts.append(f'<line class="grid" x1="{x:.2f}" y1="{top}" x2="{x:.2f}" y2="{top + plot_h}"/>')
    parts.append(f'<text x="{x:.2f}" y="{top + plot_h + 23}" font-size="11" text-anchor="middle" class="muted">{batch}</text>')

parts.append(f'<line class="axis" x1="{left}" y1="{top + plot_h}" x2="{left + plot_w}" y2="{top + plot_h}"/>')
parts.append(f'<line class="axis" x1="{left}" y1="{top}" x2="{left}" y2="{top + plot_h}"/>')
parts.append(f'<text x="{left + plot_w / 2:.2f}" y="{height - 28}" font-size="13" text-anchor="middle">Batch size (fixture decodes inside one FastRPC call, log2 spacing)</text>')
parts.append(f'<text transform="translate(22 {top + plot_h / 2:.2f}) rotate(-90)" font-size="13" text-anchor="middle">us/decode</text>')

for level in levels:
    series = sorted((r for r in rows if r["level"] == level), key=lambda r: r["batch"])
    color = colors.get(level, "#4b5563")
    points = " ".join(f'{x_pos(r["batch"]):.2f},{y_pos(r["us"]):.2f}' for r in series)
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

append_markdown() {
    {
        echo
        echo "## Real-Device Paper-Style Batch-Size Sweep"
        echo
        echo "Status: complete"
        echo
        echo "Generated by:"
        echo
        echo '```sh'
        echo "scripts/measure_android.sh --suite paper-batch --repeats $REPEATS"
        echo '```'
        echo
        echo "Measurement setup:"
        echo
        echo "- ADB: \`$ADB\`"
        echo "- Backend: \`labs/fastest\` FastRPC cDSP fastest path"
        echo "- Batch sizes: \`$PAPER_BATCH_SIZES\`"
        echo "- RPCs per measured point: \`1\`"
        echo "- Input data: fixture corpus compiled into DSP binary"
        echo
        echo "Chart:"
        echo
        echo "- SVG: \`$CHART_SVG\`"
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

echo "[paper-batch] ADB=$ADB"
echo "[paper-batch] OUT_ROOT=$OUT_ROOT"
echo "[paper-batch] RESULT_MD=$RESULT_MD"
echo "[paper-batch] LEVELS=$LEVELS REPEATS=$REPEATS"
echo "[paper-batch] PAPER_BATCH_SIZES=$PAPER_BATCH_SIZES"

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
        echo "batch_sizes=$PAPER_BATCH_SIZES"
        echo "device_dir=$device_dir"
    } > "$level_dir/run.info"

    for repeat in $(seq 1 "$REPEATS"); do
        repeat_dir="$level_dir/run${repeat}"
        mkdir -p "$repeat_dir"

        for batch in $PAPER_BATCH_SIZES; do
            log="$repeat_dir/batch_${batch}.log"
            echo "=== $label run $repeat/$REPEATS paper-batch batch=$batch ==="
            android_device_run_fastrpc_remote "$device_dir" "$log" paper-batch "$batch"
            csv_row_from_log "$repeat" "$level" "$batch" "$log" "$repeat_dir"
        done
    done
done

aggregate_results
write_svg_chart
append_markdown

echo "[paper-batch] wrote $SUMMARY_CSV"
echo "[paper-batch] wrote $AGG_CSV"
echo "[paper-batch] wrote $CHART_SVG"
echo "[paper-batch] appended $RESULT_MD"
