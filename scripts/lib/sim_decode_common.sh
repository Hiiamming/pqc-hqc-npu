#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

variant="${VARIANT:-fastest}"
level="${LEVEL:-128}"
bench="${BENCH:-decode}"
iters="${ITERS:-10}"
stage="${STAGE:-0}"
substage="${SUBSTAGE:-4}"

usage() {
    cat >&2 <<'EOF'
Usage: scripts/lib/sim_decode_common.sh --variant scalar|fastest --level 128|192|256
                                        --bench decode|stage|substage --iters N
                                        [--stage N] [--substage N]
EOF
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --variant) variant="$2"; shift 2 ;;
        --level) level="$2"; shift 2 ;;
        --bench) bench="$2"; shift 2 ;;
        --iters) iters="$2"; shift 2 ;;
        --stage) stage="$2"; shift 2 ;;
        --substage) substage="$2"; shift 2 ;;
        -h|--help) usage; exit 0 ;;
        *) echo "ERROR: unknown argument: $1" >&2; usage; exit 1 ;;
    esac
done

case "$variant" in
    scalar)
        lab="$ROOT_DIR/labs/scalar"
        backend_label="Hexagon scalar"
        hvx_flags=()
        extra_sources=("$lab/src/common/fft.c")
        ;;
    fastest)
        lab="$ROOT_DIR/labs/fastest"
        backend_label="Hexagon fastest HVX/HMX path"
        hvx_flags=(-mhvx -mhvx-length=128B)
        extra_sources=()
        ;;
    *)
        echo "ERROR: simulator common runner supports only scalar and fastest" >&2
        exit 1
        ;;
esac

case "$level" in
    128) hqc_id="1"; hqc_label="HQC-128" ;;
    192) hqc_id="3"; hqc_label="HQC-192" ;;
    256) hqc_id="5"; hqc_label="HQC-256" ;;
    *) echo "ERROR: level must be 128, 192, or 256" >&2; exit 1 ;;
esac

prefix="HQC${hqc_id}"
project_dir="$lab"
shared_dir="$ROOT_DIR/shared"
script_dir="$project_dir/scripts"
fixture_c="$shared_dir/fixtures/hqc${hqc_id}_decode_fixture.c"
fixture_gen="$ROOT_DIR/labs/scalar/scripts/gen_hqc${hqc_id}_decode_fixture.sh"

resolve_tutorial_root() {
    if [ -n "${HEXAGON_TUTORIAL_ROOT:-}" ]; then
        printf '%s\n' "$HEXAGON_TUTORIAL_ROOT"
    elif [ -d "$ROOT_DIR/../tools/hexagon-sdk" ]; then
        cd "$ROOT_DIR/.." && pwd
    elif [ -d "$ROOT_DIR/tools/hexagon-sdk" ]; then
        cd "$ROOT_DIR" && pwd
    elif [ -d "$ROOT_DIR/labs/tools/hexagon-sdk" ]; then
        cd "$ROOT_DIR/labs" && pwd
    else
        cd "$ROOT_DIR/.." && pwd
    fi
}

tutorial_root="$(resolve_tutorial_root)"
hexagon_sdk_root="${HEXAGON_SDK_ROOT:-$tutorial_root/tools/hexagon-sdk}"
tools_bin="$hexagon_sdk_root/tools/HEXAGON_Tools/19.0.04/Tools/bin"
h2_install="$tutorial_root/tools/h2-install"
h2_kernel="$tutorial_root/tools/hexagon-hypervisor/kernel/include"

if [ -L "$h2_install" ]; then
    h2_root="$(dirname "$(readlink -f "$h2_install")")"
    h2_kernel="$h2_root/kernel/include"
fi

clang="$tools_bin/hexagon-clang"
sim="$tools_bin/hexagon-sim"
booter="$h2_install/bin/booter"

for f in "$clang" "$sim" "$booter"; do
    if [ ! -f "$f" ]; then
        echo "ERROR: $f not found. Run install_tools.sh first or set HEXAGON_TUTORIAL_ROOT/HEXAGON_SDK_ROOT." >&2
        exit 1
    fi
done

if [ ! -f "$fixture_c" ]; then
    if [ ! -f "$fixture_gen" ]; then
        echo "ERROR: fixture generator not found: $fixture_gen" >&2
        exit 1
    fi
    bash "$fixture_gen"
fi

defines=(-DARCHV=75 "-D${prefix}_BENCH_ITERS=$iters")
case "$bench" in
    decode)
        demo="$project_dir/demos/hqc${hqc_id}_decode_bench.c"
        out="$project_dir/build/hqc${hqc_id}_decode_bench_hexagon"
        bench_label="decode benchmark"
        ;;
    stage)
        demo="$project_dir/demos/hqc${hqc_id}_decode_stage_bench.c"
        out="$project_dir/build/hqc${hqc_id}_decode_stage_bench_hexagon_stage${stage}"
        bench_label="stage benchmark, stage=$stage"
        defines+=("-D${prefix}_STAGE=$stage")
        ;;
    substage)
        demo="$project_dir/demos/hqc${hqc_id}_decode_substage_bench.c"
        out="$project_dir/build/hqc${hqc_id}_decode_substage_bench_hexagon_stage${substage}"
        bench_label="substage benchmark, substage=$substage"
        defines+=(-DHQC_ENABLE_SUBSTAGE_BENCH=1 "-D${prefix}_SUBSTAGE=$substage")
        ;;
    *)
        echo "ERROR: bench must be decode, stage, or substage" >&2
        exit 1
        ;;
esac

if [ ! -f "$demo" ]; then
    echo "ERROR: $bench bench is not available for $variant $hqc_label" >&2
    exit 1
fi

mkdir -p "$(dirname "$out")"

echo "=== Compiling $hqc_label $bench_label for $backend_label, iters=$iters ==="
"$clang" -O2 -mv75 \
    -ffunction-sections -fdata-sections \
    "${hvx_flags[@]}" \
    -mhmx \
    "${defines[@]}" \
    -I "$shared_dir/fixtures" \
    -I "$shared_dir/src/common" \
    -I "$project_dir/src/common" \
    -I "$shared_dir/src/ref" \
    -I "$project_dir/src/ref" \
    -I "$project_dir/src/ref/hqc-${hqc_id}" \
    -I "$h2_install/include" \
    -I "$h2_kernel" \
    -moslib=h2 \
    -Wl,-L,"$h2_install/lib" \
    -Wl,--section-start=.start=0x02000000 \
    -Wl,--gc-sections \
    -o "$out" \
    "$demo" \
    "$fixture_c" \
    "${extra_sources[@]}" \
    "$project_dir/src/ref/gf.c" \
    "$project_dir/src/ref/reed_muller.c" \
    "$project_dir/src/ref/reed_solomon.c"
echo "  -> $out"

echo
echo "=== Running on hexagon-sim ==="
"$sim" --mv75 --mhmx 1 --simulated_returnval \
    -- "$booter" \
    --ext_power 1 \
    --use_ext 1 \
    --fence_hi 0xfe000000 \
    "$out"
