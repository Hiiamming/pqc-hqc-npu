# HQC Current Real-Device Measurement Results

Status: complete for the current cloud2 run.

This file records the current measurement pass. It is intentionally separate
from `README_result_whole.md` so the previous accumulated result log remains
unchanged.

Run setup:

- Date: 2026-05-30
- Device access: Windows ADB through WSL at `/mnt/c/Temp/ADB/platform-tools/adb.exe`
- Device serial observed before run: `38faf811`
- Device model observed before run: `Kalama for arm64`
- Direct energy source: Android power-supply voltage/current sampled by `scripts/measure_board_energy.sh`
- Process CPU source: per-thread `/proc/<pid>/task/*/stat` ticks sampled by `scripts/measure_process_cpu.sh`
- qprof context: disabled for the main direct-energy run (`RUN_QPROF_CONTEXT=0`)
- Output policy: this run writes detailed tables here, not to `README_result_whole.md`

## Real-Device Mean/Std Rerun - Current Cloud2

Status: complete

Generated from chunked runs because background WSL process launch was unreliable, while foreground WSL + ADB was stable. Each chunk used the same benchmark configuration and wrote raw CSV output under `results/qprof/`.

Device:

```text
serial=38faf811
model=Kalama for arm64
adb=/mnt/c/Temp/ADB/platform-tools/adb.exe
```

Effective command shape for every chunk:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
LEVELS="<128|192|256 or 128 192 256>" \
REPEATS=1 \
RUN_SANITY=0 \
RUN_DIRECT_ENERGY=1 \
DIRECT_SAMPLE_INTERVAL=0.1 \
DIRECT_IDLE_SECONDS=10 \
DIRECT_IDLE_POSITION=both \
RUN_QPROF_CONTEXT=0 \
RESULT_MD=results/qprof/current_20260530_cloud2_chunks.md \
RUN_ID=current_20260530_cloud2_<chunk> \
scripts/measure_android_decode_stats.sh
```

Combined output directory:

```text
results/qprof/qprof_hqc_whole_stats_current_20260530_cloud2_combined
```

## Fixture Generation - Current Cloud2

The CPU and NPU builds use the same generated decode fixture corpus under
`shared/fixtures/`. The generator sources are:

- `shared/tools/gen_hqc1_decode_fixture.c` for HQC-128
- `shared/tools/gen_hqc3_decode_fixture.c` for HQC-192
- `shared/tools/gen_hqc5_decode_fixture.c` for HQC-256

The generator wrapper scripts are:

- `labs/scalar/scripts/gen_hqc1_decode_fixture.sh`
- `labs/scalar/scripts/gen_hqc3_decode_fixture.sh`
- `labs/scalar/scripts/gen_hqc5_decode_fixture.sh`

Generation method:

- Each generator compiles against the reference implementation in `git/hqc_gitlab/src` by default.
- `HQC_REFERENCE_SRC` can override the reference tree, but this run used the default.
- The PRNG default seed is `0x128c0de5`; it can be overridden with `HQC_FIXTURE_SEED`.
- If `HQC_FIXTURE_SEED` is set to `0`, the generator coerces the internal state to `1`.
- The PRNG is a local xorshift32 sequence used for messages, RS-symbol positions, and corruption masks.
- Each fixture starts from a reference `code_encode()` codeword, injects scheduled RS-symbol errors, then validates the fixture by running reference `code_decode()` and checking the recovered message matches the original message.
- Build paths regenerate fixtures when the generator source or generator script is newer than the generated fixture `.c`, avoiding stale fixture reuse after generator changes.

Fixture characteristics:

| HQC | Fixture macro | Fixtures | Message bytes `PARAM_K` | Codeword bytes `VEC_N1N2_SIZE_BYTES` | RS blocks `PARAM_N1` | `PARAM_DELTA` | Error schedule |
| --- | --- | ---: | ---: | ---: | ---: | ---: | --- |
| HQC-128 | `HQC1_FIXTURE_COUNT` | 256 | 16 | 2208 | 46 | 15 | `i % 16` |
| HQC-192 | `HQC3_FIXTURE_COUNT` | 256 | 24 | 4480 | 56 | 16 | `i % 17` |
| HQC-256 | `HQC5_FIXTURE_COUNT` | 256 | 32 | 7200 | 90 | 29 | `i % 30` |

For fixture index `i`, the generator sets:

```text
rs_symbol_errors[i] = i % (PARAM_DELTA + 1)
```

For each injected RS-symbol error, it selects a previously unused RS block,
computes the byte offset as `position * (PARAM_N2 / 8)`, and corrupts the full
Reed-Muller block rather than one byte. This is the current fixed-fixture
behavior intended to make HQC-192 and HQC-256 exercise the RS-error path instead
of the clean-syndrome fast path.

More explicitly, one Reed-Solomon symbol is one byte before the duplicated
Reed-Muller layer. The reference encoder first produces a Reed-Solomon codeword,
then encodes each RS byte with the duplicated Reed-Muller code:

```text
1 RS byte -> MULTIPLICITY copies of a 128-bit RM codeword
MULTIPLICITY = ceil(PARAM_N2 / 128)
```

The fixture generator therefore treats one RS-symbol position as the whole
RM-encoded chunk for that symbol:

| HQC | `PARAM_N2` | RM multiplicity | Bytes per RS-symbol chunk | Example byte range for `position=2` |
| --- | ---: | ---: | ---: | --- |
| HQC-128 | 384 bits | 3 | 48 | `96..143` |
| HQC-192 | 640 bits | 5 | 80 | `160..239` |
| HQC-256 | 640 bits | 5 | 80 | `160..239` |

For HQC-128, for example, `position=2` means:

```text
offset = 2 * (384 / 8) = 96
bytes 96..111  = RM copy 0, 128 bits
bytes 112..127 = RM copy 1, 128 bits
bytes 128..143 = RM copy 2, 128 bits
```

The generator corrupts all bytes in that chunk:

```c
for (size_t j = 0; j < PARAM_N2 / 8; ++j) {
    codeword[offset + j] ^= pattern_j;
}
```

This matters because flipping only one byte or a few bits inside the
RM-encoded chunk can be corrected by the Reed-Muller decoder, leaving the
decoded RS byte unchanged and producing a clean Reed-Solomon syndrome. Corrupting
the whole chunk makes the selected RS byte become a real RS-symbol error after
RM decoding, so the Reed-Solomon stages are exercised by the benchmark corpus.

Profiler settings:

- Direct energy source: Android power-supply voltage/current sampled by `scripts/measure_board_energy.sh`
- `DIRECT_SAMPLE_INTERVAL=0.1`
- `DIRECT_IDLE_SECONDS=10`
- `DIRECT_IDLE_POSITION=both`
- qprof context enabled: `0`
- Process CPU source: per-thread `/proc/<pid>/task/*/stat` ticks sampled by `scripts/measure_process_cpu.sh`
- `PROCESS_CPU_SAMPLE_INTERVAL=0.02`
- CPU target decodes: `32000`
- NPU target decodes: `32000`
- CPU path: scalar ARM64 baseline
- NPU path: current non-worker `labs/fastest` FastRPC build
- FastRPC build arch: `HEXAGON_ARCH=v73`
- Fixture count: `256` fixtures for each HQC level
- Decode benchmark iterations: `ceil(32000 / 256) = 125`
- Total decodes per measured workload: `125 * 256 = 32000`

## Direct Energy Aggregate - Current Cloud2

How this table is measured:

- Workload: CPU rows run the ARM64 scalar binary; NPU rows run the non-worker `labs/fastest` FastRPC binary.
- Repeat count: `n=5`; each repeat is one direct-energy measurement per backend and HQC level.
- Columns ending in `mean` are the average over the 5 repeats; columns ending in `std` are the sample standard deviation across those repeats.
- Energy source: `scripts/measure_board_energy.sh` samples Android power-supply voltage/current while the workload runs, with idle sampled before/after the workload.
- `delta W`: active average board power minus idle average board power.
- `uJ`: `delta_W * elapsed_s / total_decodes * 1e6`.
- `speedup`: CPU `us/decode` divided by NPU `us/decode` for the same HQC level, computed per repeat and then averaged.
- `energy gain`: CPU `uJ/decode` divided by NPU `uJ/decode` for the same HQC level, computed per repeat and then averaged. Because this is a mean of per-repeat ratios, it can differ from `CPU uJ/decode mean / NPU uJ/decode mean`.

| HQC | Backend | n | us/decode mean | us/decode std | decodes/s mean | decodes/s std | delta W mean | delta W std | uJ/decode mean | uJ/decode std | decodes/s/W mean | decodes/s/W std | Speedup mean | Speedup std | Energy gain mean | Energy gain std |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| HQC-128 | CPU scalar | 5 | 81.144 | 0.134 | 12323.858 | 20.349 | 2.185805666 | 0.057040172 | 189.108 | 7.026 | 5641.362 | 154.915 | 1.00 | 0.00 | 1.00 | 0.00 |
| HQC-128 | NPU fastest non-CT | 5 | 39.173 | 0.018 | 25527.661 | 11.772 | 0.236460819 | 0.035554898 | 10.593 | 1.572 | 110083.297 | 17715.105 | 2.07 | 0.00 | 18.13 | 2.40 |
| HQC-192 | CPU scalar | 5 | 103.531 | 0.080 | 9658.910 | 7.435 | 2.269756307 | 0.073685597 | 246.480 | 11.319 | 4258.922 | 132.444 | 1.00 | 0.00 | 1.00 | 0.00 |
| HQC-192 | NPU fastest non-CT | 5 | 56.065 | 0.117 | 17836.629 | 37.223 | 0.375542466 | 0.122856779 | 23.645 | 8.134 | 53040.740 | 21598.242 | 1.85 | 0.00 | 11.77 | 4.94 |
| HQC-256 | CPU scalar | 5 | 228.082 | 0.230 | 4384.395 | 4.416 | 2.509910519 | 0.067365159 | 584.573 | 19.459 | 1747.860 | 47.842 | 1.00 | 0.00 | 1.00 | 0.00 |
| HQC-256 | NPU fastest non-CT | 5 | 116.333 | 0.082 | 8596.032 | 6.087 | 0.291509307 | 0.052371207 | 36.078 | 6.732 | 30497.824 | 7046.164 | 1.96 | 0.00 | 16.81 | 4.17 |

## Process CPU Aggregate - Current Cloud2

How this table is measured:

- Workload: same decode binaries as the direct energy table, wrapped by `scripts/measure_process_cpu.sh`.
- Source: the script samples per-thread CPU ticks from `/proc/<pid>/task/*/stat` while the process is alive.
- Columns ending in `mean` are the average over the 5 repeats; columns ending in `std` are the sample standard deviation across those repeats.
- `process CPU %`: process CPU time consumed during wall-clock interval, normalized by elapsed wall time.
- `CPU ms/decode`: process CPU milliseconds divided by total decodes.
- `CPU reduction %`: `(1 - npu_cpu_ms_per_decode / cpu_cpu_ms_per_decode) * 100` for the same HQC level, computed per repeat and then averaged.

| HQC | Backend | n | process CPU % mean | process CPU % std | CPU ms/decode mean | CPU ms/decode std | CPU reduction % mean | CPU reduction % std |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| HQC-128 | CPU scalar | 5 | 93.606 | 1.162 | 0.0803125 | 0.0006629 | 0.000 | 0.000 |
| HQC-128 | NPU fastest non-CT | 5 | 1.498 | 0.885 | 0.0006875 | 0.0004075 | 99.145 | 0.505 |
| HQC-192 | CPU scalar | 5 | 94.538 | 0.894 | 0.1025625 | 0.0008672 | 0.000 | 0.000 |
| HQC-192 | NPU fastest non-CT | 5 | 0.678 | 0.544 | 0.0004375 | 0.0003563 | 99.575 | 0.344 |
| HQC-256 | CPU scalar | 5 | 97.243 | 0.516 | 0.2268750 | 0.0007329 | 0.000 | 0.000 |
| HQC-256 | NPU fastest non-CT | 5 | 0.606 | 0.133 | 0.0007500 | 0.0001712 | 99.669 | 0.075 |

## FastRPC Boundary Overhead - Current Cloud2

Status: complete

Generated by chunked runs equivalent to:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
OUT_ROOT=/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/fastrpc_overhead_current_cloud2_chunks \
LEVELS="128 192 256" REPEATS=5 HEXAGON_ARCH=v73 \
PING_CALLS=10000 OPEN_CLOSE_CALLS=100 PAYLOAD_CALLS=10000 \
DECODE_ONE_CALLS=10000 BENCH_ITERS=125 \
scripts/measure_android_fastrpc_overhead.sh
```

How this table is measured:

- Workload: `scripts/measure_android_fastrpc_overhead.sh` builds the FastRPC host/skel for `HEXAGON_ARCH=v73`, pushes it to the Android target, then runs special `hqc_host` boundary modes.
- Device: current Cloud2 device, Android `Kalama for arm64`, serial `38faf811`.
- Fixture corpus: same current fixed fixture generator as the direct-energy/process-CPU runs, with `256` fixtures per HQC level, seed `0x128c0de5`, and RS-symbol error schedule `fixture_index % (PARAM_DELTA + 1)`.
- Fixture corruption behavior: for each selected RS-symbol error, the generator corrupts the full Reed-Muller block at `position * (PARAM_N2 / 8)`, so HQC-192/HQC-256 exercise the RS-error path instead of the clean-syndrome fast path.
- `bench` mode: `BENCH_ITERS * fixture_count = 125 * 256 = 32000` decodes per repeat. This is the batched decode path, so FastRPC boundary cost is amortized.
- `decode-one` mode: `10000` one-decode RPC calls per repeat. The DSP advances an internal fixture index and wraps modulo `256`, so each repeat cycles through the fixed corpus about `39` full times plus `16` fixtures.
- `ping`: lightweight RPC call approximating base FastRPC boundary cost.
- `open-close`: repeated FastRPC session setup/teardown, not steady-state per-decode cost.
- Payload probes: input payloads are `2304`, `4480`, and `7296` bytes for HQC-128/192/256; output payload is `128` bytes for all levels.
- Allocation modes: `malloc` is normal host allocation; `rpcmem-cached` and `rpcmem-uncached` use rpcmem buffers with cached/uncached mapping.
- All rows in the aggregate table are microseconds per RPC/call, except `bench batched decode`, which is microseconds per decode.
- `std us` is sample standard deviation over 5 repeats. `variance us^2` is `std^2`.

Boundary summary:

| HQC | n | decode-one mean us | bench mean us | boundary tax mean us | decode-one / bench |
| --- | ---: | ---: | ---: | ---: | ---: |
| HQC-128 | 5 | 590.640 | 39.205 | 551.435 | 15.07 |
| HQC-192 | 5 | 604.201 | 56.234 | 547.967 | 10.74 |
| HQC-256 | 5 | 629.596 | 116.104 | 513.492 | 5.42 |

Aggregate table:

| HQC | Metric | n | mean us | std us | variance us^2 |
| --- | --- | ---: | ---: | ---: | ---: |
| HQC-128 | open-close | 5 | 52537.465 | 1225.600 | 1502096.019 |
| HQC-128 | ping | 5 | 552.804 | 8.107 | 65.724 |
| HQC-128 | payload-in malloc | 5 | 572.112 | 7.604 | 57.819 |
| HQC-128 | payload-out malloc | 5 | 551.184 | 3.812 | 14.529 |
| HQC-128 | payload-inout malloc | 5 | 573.082 | 6.907 | 47.704 |
| HQC-128 | payload-in rpcmem-cached | 5 | 575.093 | 6.127 | 37.542 |
| HQC-128 | payload-out rpcmem-cached | 5 | 554.856 | 5.014 | 25.142 |
| HQC-128 | payload-inout rpcmem-cached | 5 | 576.955 | 4.567 | 20.854 |
| HQC-128 | payload-in rpcmem-uncached | 5 | 594.525 | 5.673 | 32.187 |
| HQC-128 | payload-out rpcmem-uncached | 5 | 570.322 | 4.025 | 16.198 |
| HQC-128 | payload-inout rpcmem-uncached | 5 | 602.544 | 8.231 | 67.757 |
| HQC-128 | decode-one | 5 | 590.640 | 11.378 | 129.460 |
| HQC-128 | bench batched decode | 5 | 39.205 | 0.032 | 0.001 |
| HQC-192 | open-close | 5 | 54817.582 | 1006.490 | 1013021.787 |
| HQC-192 | ping | 5 | 530.990 | 14.395 | 207.224 |
| HQC-192 | payload-in malloc | 5 | 580.267 | 17.132 | 293.503 |
| HQC-192 | payload-out malloc | 5 | 543.004 | 13.815 | 190.845 |
| HQC-192 | payload-inout malloc | 5 | 589.485 | 12.517 | 156.687 |
| HQC-192 | payload-in rpcmem-cached | 5 | 587.533 | 9.792 | 95.890 |
| HQC-192 | payload-out rpcmem-cached | 5 | 542.444 | 15.875 | 252.012 |
| HQC-192 | payload-inout rpcmem-cached | 5 | 581.914 | 21.147 | 447.185 |
| HQC-192 | payload-in rpcmem-uncached | 5 | 593.492 | 21.817 | 475.960 |
| HQC-192 | payload-out rpcmem-uncached | 5 | 556.851 | 21.081 | 444.404 |
| HQC-192 | payload-inout rpcmem-uncached | 5 | 593.801 | 13.729 | 188.496 |
| HQC-192 | decode-one | 5 | 604.201 | 6.246 | 39.010 |
| HQC-192 | bench batched decode | 5 | 56.234 | 0.385 | 0.148 |
| HQC-256 | open-close | 5 | 57386.133 | 1018.305 | 1036944.652 |
| HQC-256 | ping | 5 | 535.708 | 14.638 | 214.260 |
| HQC-256 | payload-in malloc | 5 | 609.966 | 14.762 | 217.923 |
| HQC-256 | payload-out malloc | 5 | 548.496 | 15.511 | 240.583 |
| HQC-256 | payload-inout malloc | 5 | 595.244 | 19.227 | 369.675 |
| HQC-256 | payload-in rpcmem-cached | 5 | 594.196 | 11.203 | 125.500 |
| HQC-256 | payload-out rpcmem-cached | 5 | 545.547 | 13.589 | 184.667 |
| HQC-256 | payload-inout rpcmem-cached | 5 | 592.571 | 20.153 | 406.136 |
| HQC-256 | payload-in rpcmem-uncached | 5 | 584.628 | 20.312 | 412.578 |
| HQC-256 | payload-out rpcmem-uncached | 5 | 547.815 | 11.846 | 140.327 |
| HQC-256 | payload-inout rpcmem-uncached | 5 | 598.935 | 16.609 | 275.845 |
| HQC-256 | decode-one | 5 | 629.596 | 13.215 | 174.625 |
| HQC-256 | bench batched decode | 5 | 116.104 | 0.130 | 0.017 |

## Simulator HQC-128 Benchmark(1) Selected Substages - Current

Status: complete

This refreshes the HQC-128 full-decode and selected substage measurements used
by `git/Benchmark (1).tex`, following the paired `iters=1`/`iters=3` methodology
from `README_result_whole.md`.

Generated by:

```sh
export HEXAGON_TUTORIAL_ROOT=/home/hiiamming/Code/test/hexagon-tutorial

for variant in scalar fastest; do
  scripts/sim_decode.sh --variant "$variant" --level 128 --bench decode --iters 1
  scripts/sim_decode.sh --variant "$variant" --level 128 --bench decode --iters 3
done

for variant in scalar fastest; do
  for substage in 2 3 4 5; do
    scripts/sim_decode.sh --variant "$variant" --level 128 --bench substage --substage "$substage" --iters 1
    scripts/sim_decode.sh --variant "$variant" --level 128 --bench substage --substage "$substage" --iters 3
  done
done
```

How this table is measured:

- Workload: Hexagon simulator only. No Android latency, power, energy, or process CPU is measured in this section.
- Fixture corpus: current generated HQC-128 corpus, `256` fixtures, seed `0x128c0de5`.
- Fixture error schedule: fixture index `i` uses `i % (PARAM_DELTA + 1)` RS-symbol errors; for HQC-128, `PARAM_DELTA=15`.
- Full decode: paired `iters=1` and `iters=3`; the delta is `768 - 256 = 512` decodes.
- Full decode `Pcycles/decode`: `(Pcycles_i3 - Pcycles_i1) / 512`.
- Selected substages: these are exactly the four substage rows used in `git/Benchmark (1).tex`: `2=rm_hadamard`, `3=rm_peak`, `4=rs_syndrome`, and `5=rs_elp`.
- RM substage delta ops: HQC-128 has `PARAM_N1=46` RM blocks per decode, so paired `iters=1`/`iters=3` gives `2 * 256 * 46 = 23552` delta ops.
- RS substage delta ops: one RS op per fixture/decode, so paired `iters=1`/`iters=3` gives `2 * 256 = 512` delta ops.
- RM `Pcycles/decode`: `Pcycles/op * 46`. RS `Pcycles/decode`: `Pcycles/op` directly.
- `Status`: both paired runs must report `PASS`.

### Simulator HQC-128 Full Decode

| HQC | Backend | i1 Pcycles | i3 Pcycles | Delta decodes | Pcycles/decode | Insns/decode | Speedup vs scalar | Status |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| HQC-128 | CPU scalar | 247685868 | 736012680 | 512 | 953763.305 | 862807.201 | 1.00x | PASS |
| HQC-128 | NPU fastest | 15107910 | 36341124 | 512 | 41471.121 | 32757.119 | 23.00x | PASS |

### Simulator HQC-128 Selected Substage

| Substage | Backend | i1 Pcycles | i3 Pcycles | Delta ops | Pcycles/op | Pcycles/decode | Insns/op | Speedup vs scalar | Status |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| Reed-Muller Hadamard (`2`) | CPU scalar | 578998362 | 713744082 | 23552 | 5721.201 | 263175.234 | 5284.177 | 1.00x | PASS |
| Reed-Muller Hadamard (`2`) | NPU fastest | 34444119 | 43634331 | 23552 | 390.209 | 17949.633 | 346.181 | 14.66x | PASS |
| Reed-Muller find peak (`3`) | CPU scalar | 529855362 | 566318592 | 23552 | 1548.201 | 71217.246 | 1928.145 | 1.00x | PASS |
| Reed-Muller find peak (`3`) | NPU fastest | 31404159 | 34517757 | 23552 | 132.201 | 6081.246 | 70.145 | 11.71x | PASS |
| Reed-Solomon syndrome (`4`) | CPU scalar | 553176402 | 636280212 | 512 | 162312.129 | 162312.129 | 148867.096 | 1.00x | PASS |
| Reed-Solomon syndrome (`4`) | NPU fastest | 30748239 | 32548701 | 512 | 3516.527 | 3516.527 | 4536.244 | 46.16x | PASS |
| Reed-Solomon ELP (`5`) | CPU scalar | 542239275 | 603471981 | 512 | 119595.129 | 119595.129 | 105316.586 | 1.00x | PASS |
| Reed-Solomon ELP (`5`) | NPU fastest | 31531236 | 34900842 | 512 | 6581.262 | 6581.262 | 4898.504 | 18.17x | PASS |

## Raw Files - Current Cloud2

- Combined summary CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/qprof/qprof_hqc_whole_stats_current_20260530_cloud2_combined/summary.csv`
- Direct aggregate CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/qprof/qprof_hqc_whole_stats_current_20260530_cloud2_combined/aggregate_direct.csv`
- Process aggregate CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/qprof/qprof_hqc_whole_stats_current_20260530_cloud2_combined/aggregate_process.csv`
- Per-chunk markdown/log: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/qprof/current_20260530_cloud2_chunks.md`
- FastRPC boundary summary CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/fastrpc_overhead_current_cloud2_combined/summary.csv`
- FastRPC boundary aggregate CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/fastrpc_overhead_current_cloud2_combined/aggregate.csv`
- Simulator HQC-128 Benchmark(1) raw logs: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/sim_decode/current_20260530_hqc128_benchmark1/`
- Simulator HQC-128 full-decode aggregate CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/sim_decode/current_20260530_hqc128_benchmark1/aggregate_full_decode.csv`
- Simulator HQC-128 selected-substage aggregate CSV: `/home/hiiamming/Code/test/hexagon-tutorial/hqc/results/sim_decode/current_20260530_hqc128_benchmark1/aggregate_selected_substage.csv`
