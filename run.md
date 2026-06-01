# HQC Benchmark Runners

## Simulator Decode Runner

The simulator entrypoint is:

```sh
scripts/sim_decode.sh --variant scalar|fastest|ct --level 128|192|256 \
  --bench decode|stage|substage --iters N [--stage N] [--substage N]
```

For the current refactor, `scalar` and `fastest` are handled by the shared
simulator runner in `scripts/lib/sim_decode_common.sh`. The `ct` variant is
still delegated to the older per-lab wrapper scripts and is not part of this
simulator-script cleanup pass.

### What It Measures

`scripts/sim_decode.sh` builds a Hexagon simulator binary with `hexagon-clang`
and runs it with `hexagon-sim` through the H2 booter. It does not run on the
Android ARM CPU and it does not use FastRPC.

- `--variant scalar`: builds `labs/scalar` and runs the scalar algorithm on
  `hexagon-sim`. This is the simulator scalar baseline.
- `--variant fastest`: builds `labs/fastest` with HVX/HMX flags and runs it on
  `hexagon-sim`. This is the simulator fastest HVX/HMX path.
- `--variant ct`: legacy path for `labs/ct`; currently delegated unchanged.

The benchmark binary prints `result=PASS` or `result=FAIL`. A valid measurement
requires `PASS`.

The simulator reports raw totals at the end:

```text
Total: Insns=<N> Pcycles=<N>
```

For Benchmark.tex-style estimates, run paired `--iters 1` and `--iters 3`
measurements and subtract:

```text
Pcycles/decode = (Pcycles_i3 - Pcycles_i1) / ((3 - 1) * fixture_count)
```

For the current fixture corpus, `fixture_count = 256`, so full decode uses
`delta_decodes = 512`.

### Options

- `--variant scalar|fastest|ct`
  Selects the lab source tree. Use `scalar` and `fastest` for the current
  refactored simulator path.
- `--level 128|192|256`
  Selects HQC-128, HQC-192, or HQC-256. Internally these map to `hqc1`, `hqc3`,
  and `hqc5`.
- `--bench decode|stage|substage`
  Selects the benchmark binary.
- `--iters N`
  Sets `HQC*_BENCH_ITERS` at compile time.
- `--stage N`
  Used only with `--bench stage`. Stage `0` is full, `1` is Reed-Muller, and
  `2` is Reed-Solomon where that lab provides the stage bench.
- `--substage N`
  Used only with `--bench substage`.

### Common Calls

Full decode for all HQC levels used by the paper table:

```sh
export HEXAGON_TUTORIAL_ROOT=/home/hiiamming/Code/test/hexagon-tutorial

for variant in scalar fastest; do
  for level in 128 192 256; do
    scripts/sim_decode.sh --variant "$variant" --level "$level" \
      --bench decode --iters 1
    scripts/sim_decode.sh --variant "$variant" --level "$level" \
      --bench decode --iters 3
  done
done
```

Selected HQC-128 substages:

```sh
for variant in scalar fastest; do
  for substage in 2 3 4 5; do
    scripts/sim_decode.sh --variant "$variant" --level 128 \
      --bench substage --substage "$substage" --iters 1
    scripts/sim_decode.sh --variant "$variant" --level 128 \
      --bench substage --substage "$substage" --iters 3
  done
done
```

Substage meanings for the HQC-128 table:

- `2`: Reed-Muller Hadamard
- `3`: Reed-Muller find peak
- `4`: Reed-Solomon syndrome
- `5`: Reed-Solomon error-locator polynomial

For Reed-Muller substages, convert per-op cost to per-decode cost by multiplying
by `PARAM_N1 = 46` for HQC-128. Reed-Solomon substage ops are already one op per
decode.

## Android Real-Device Fastest Runner

The real-device results in `references/Benchmark.tex` compare:

- `CPU scalar`: the ARM64 scalar decoder running directly on the Android CPU.
- `NPU-supported`: the non-worker `labs/fastest` decoder running on the cDSP
  through FastRPC. The Android `hqc_host` process submits one batched remote
  call and waits for completion.

CT and worker-pool experiments are outside this workflow.

Use Windows ADB from WSL:

```sh
export ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe

"$ADB" devices -l
"$ADB" shell 'getprop ro.product.model; getprop ro.board.platform; uname -a'
```

The stable real-device entrypoint is:

```sh
scripts/measure_android.sh --suite paper|boundary [options]
```

Common options:

- `--levels "128 192 256"`
  Selects HQC parameter sets. The default is all three levels.
- `--repeats N`
  Selects the number of measured repetitions. Paper tables use `5`.
- `--skip-build`
  Skips compilation. For the paper suite, artifacts must already be deployed.
  For the boundary suite, existing local build artifacts are still pushed to
  the device, so use this only when the local artifact matches the selected
  level.
- `--sanity`
  Runs the optional historical-baseline sanity check before measured repeats.
- `--sanity-only`
  Runs only the paper suite's historical-baseline sanity run.
- `--out-root DIR`
  Overrides the local raw-result directory.
- `--result-md FILE`
  Overrides the markdown result log written by the paper suite.

### Paper Suite: Latency, Energy, And CPU Offload

Run the full real-device paper suite:

```sh
"$ADB" devices -l

ADB="$ADB" \
HEXAGON_ARCH=v73 \
scripts/measure_android.sh --suite paper
```

Defaults:

- Levels: `128 192 256`
- Repeats: `5`
- Target workload: `32000` decodes per backend, level, and repeat
- Fixture corpus: `256` fixtures per HQC level
- Decode iterations: `ceil(32000 / 256) = 125`
- qprof during direct-energy runs: disabled

The suite builds and deploys artifacts automatically. For each HQC level and
repeat, it runs four workloads:

1. CPU scalar wrapped by direct board-energy sampling.
2. CPU scalar wrapped by process CPU sampling.
3. NPU-supported `labs/fastest` FastRPC batched decode wrapped by direct
   board-energy sampling.
4. NPU-supported `labs/fastest` FastRPC batched decode wrapped by process CPU
   sampling.

The paper protocol does not include a sanity pre-pass. For a separate
historical-baseline diagnostic, add `--sanity` before measured repeats or use
`--sanity-only`.

The direct-energy wrapper is `scripts/measure_board_energy.sh`. It samples
Android power-supply voltage and current while the benchmark runs and samples
idle power before and after the workload. The suite derives:

- `us/decode`
- `decodes/s`
- `delta W = run average W - idle average W`
- `uJ/decode`
- `decodes/s/W`
- speedup versus CPU scalar
- energy gain versus CPU scalar

The process-CPU wrapper is `scripts/measure_process_cpu.sh`. It samples
`/proc/<pid>/task/*/stat`, sums process thread ticks, and derives:

- process CPU `%`, normalized so roughly `100%` means one fully loaded CPU core
- CPU `ms/decode`
- CPU reduction versus CPU scalar

The mean, standard deviation, variance, and coefficient of variation used by
the paper are derived from repeated runs. They are not separate benchmark
modes.

For a one-repeat regression check after refactoring:

```sh
ADB="$ADB" \
HEXAGON_ARCH=v73 \
scripts/measure_android.sh \
  --suite paper \
  --levels "128" \
  --repeats 1 \
  --target-decodes 32000 \
  --out-root /tmp/hqc_android_refactor_check \
  --result-md /tmp/hqc_android_refactor_check.md
```

It writes:

- `summary.csv`: one row per measured workload
- `aggregate_direct.csv`: latency, energy, and throughput-per-watt aggregates
- `aggregate_process.csv`: process CPU aggregates
- `real_device_stats_tables.tex`: generated TeX rows

Without `--result-md`, the lower-level runner appends markdown tables to
`result.md`. Use `--result-md /tmp/<name>.md` for temporary
regression checks.

### Boundary Suite: FastRPC Overhead

`references/Benchmark.tex` reports a separate FastRPC boundary table. It is not
the same as the direct-energy table. The boundary suite measures the cost of
crossing the FastRPC boundary and shows why the main NPU-supported path batches
many decodes into one remote call.

Run the full boundary suite:

```sh
ADB="$ADB" \
HEXAGON_ARCH=v73 \
scripts/measure_android.sh --suite boundary
```

Defaults used by the stable wrapper:

- Levels: `128 192 256`
- Repeats: `5`
- `open-close`: `100` calls
- `ping`: `10000` calls
- payload probes: `10000` calls
- `decode-one`: `10000` calls
- batched `bench`: `125` iterations, or `125 * 256 = 32000` decodes

The suite runs these FastRPC host modes:

| Mode | What it measures | Manual command shape |
| --- | --- | --- |
| `open-close` | FastRPC session setup and teardown cost | `./hqc_host open-close 100` |
| `ping` | Lightweight no-op FastRPC boundary cost | `./hqc_host ping 10000` |
| `payload-in` | Host-to-cDSP buffer submission cost | `./hqc_host payload-in rpcmem-cached <bytes> 10000` |
| `payload-out` | cDSP-to-host output-buffer cost | `./hqc_host payload-out rpcmem-cached 128 10000` |
| `payload-inout` | Combined input/output-buffer cost | `./hqc_host payload-inout rpcmem-cached <in-bytes> 128 10000` |
| `decode-one` | Worst-case fine-grained offload: one FastRPC call per decode | `./hqc_host decode-one 10000` |
| `bench` | Batched cDSP decode path used by the real-device latency table | `./hqc_host bench 125` |

Payload probes run all three allocation modes:

- `malloc`
- `rpcmem-cached`
- `rpcmem-uncached`

Input payload bytes depend on the HQC level:

| HQC | Input bytes | Output bytes |
| --- | ---: | ---: |
| HQC-128 | `2304` | `128` |
| HQC-192 | `4480` | `128` |
| HQC-256 | `7296` | `128` |

The paper table reports `ping`, `decode-one`, batched `bench`, and the
`decode-one / bench` slowdown. Payload probes and `open-close` provide
supporting boundary diagnostics.

For a one-repeat boundary regression check:

```sh
ADB="$ADB" \
HEXAGON_ARCH=v73 \
scripts/measure_android.sh \
  --suite boundary \
  --levels "128" \
  --repeats 1 \
  --skip-build \
  --out-root /tmp/hqc_android_boundary_refactor_check
```

It writes one raw log per mode and repeat under:

```text
$OUT_ROOT/<timestamp>_hqc<level>/run<repeat>/
```

The default `OUT_ROOT` is `results/fastrpc_overhead`.

### Manual FastRPC Build And Run

The suites normally build and deploy automatically. To run an individual
FastRPC mode manually, build one level:

```sh
ADB="$ADB" \
HEXAGON_ARCH=v73 \
HQC_PARAM_LEVEL=128 \
HQC_DEFAULT_BENCH_ITERS=125 \
HQC_PROJECT_DIR="$PWD/labs/fastest" \
bash fastrpc/hqc/build_android_gcc_bionic.sh
```

Deploy:

```sh
device_dir=/data/local/tmp/QDC_files/hqc_manual_fastest_128

"$ADB" shell "mkdir -p '$device_dir'"
"$ADB" push fastrpc/hqc/build/hqc_host "$device_dir/"
"$ADB" push fastrpc/hqc/build/libhqc_skel.so "$device_dir/"
[ ! -f fastrpc/hqc/build/testsig-0xaa3ec42e.so ] ||
  "$ADB" push fastrpc/hqc/build/testsig-0xaa3ec42e.so "$device_dir/"
```

Run a mode:

```sh
"$ADB" shell "
  cd '$device_dir' &&
  chmod +x hqc_host &&
  export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" &&
  export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" &&
  ./hqc_host bench 125
"
```

Replace `bench 125` with any command shape from the boundary-mode table.

### Optional qprof Diagnostic Context

qprof is diagnostic-only. A fresh board session may not include the Qualcomm
Profiler target files, so install them before use. Do not mix qprof runs into
direct-energy tables: qprof can perturb power and cDSP clock state.

Use qprof for context such as CPU load, NPU utilization, QDSP clock, HMX
utilization, MemNoc vote, and thermal counters.

Supported modes when qprof is installed:

| Mode | What it profiles |
| --- | --- |
| `idle` | Idle CPU, battery, and thermal context |
| `cpu` | CPU workload context |
| `npu0` | NPU capability `profiler:nsp-dsp-metrics` |
| `npu1` | NPU capability `profiler:nsp1-dsp-metrics` used by devices that expose NSP1 metrics |

Install the Qualcomm Profiler target files from WSL when `/vendor/bin/qprof` is
missing:

```sh
QPROF_TARGET="/mnt/c/Program Files (x86)/Qualcomm/Shared/QualcommProfiler/API/target-la/aarch64"
QPROF_DB="/mnt/c/Program Files (x86)/Qualcomm/Shared/Prof_Ext/ExtQProfiler.db"

"$ADB" root
"$ADB" remount
"$ADB" shell 'mkdir -p /vendor/bin /vendor/qprof/libs /vendor/qprof/backends /data/shared/qcom/Shared/Prof_Ext'
"$ADB" push "$QPROF_TARGET/bins/." /vendor/bin/
"$ADB" push "$QPROF_TARGET/libs/." /vendor/qprof/libs/
"$ADB" push "$QPROF_TARGET/libs/backends/." /vendor/qprof/backends/
"$ADB" push "$QPROF_DB" /data/shared/qcom/Shared/Prof_Ext/
"$ADB" shell 'chmod -R 755 /vendor/bin/qprof /vendor/bin/qmonitor-grpc-server /vendor/bin/profilerUtilityApp /vendor/qprof'
```

Check qprof capabilities:

```sh
"$ADB" shell '
  test -x /vendor/bin/qprof || {
    echo "qprof is unavailable on this device"
    exit 1
  }
  export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs
  /vendor/bin/qprof --capabilities
'
```

Run idle context:

```sh
ADB="$ADB" PROFILE_TIME=30 \
scripts/measure_qprof.sh idle hqc_idle
```

Generic command shapes for CPU and alternate NPU context:

```sh
ADB="$ADB" PROFILE_TIME=30 \
scripts/measure_qprof.sh cpu LABEL 'DEVICE_WORKLOAD_COMMAND'

ADB="$ADB" PROFILE_TIME=30 \
scripts/measure_qprof.sh npu0 LABEL 'DEVICE_WORKLOAD_COMMAND'
```

Run NPU-supported context after deploying HQC-128 artifacts:

```sh
ADB="$ADB" PROFILE_TIME=30 \
scripts/measure_qprof.sh npu1 hqc128_fastest \
  'cd /data/local/tmp/QDC_files/hqc_whole_stats/hqc128_npu_fastest_nonct &&
   chmod +x hqc_host &&
   export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp" &&
   export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic" &&
   ./hqc_host 125'
```
