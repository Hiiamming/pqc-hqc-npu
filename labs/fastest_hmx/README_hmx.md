# HQC-128 HMX-32 / HVX-Tail Baseline

## Scope

This lab is an experimental real-device HMX baseline for the HQC-128
Reed-Muller decode stage. It is intentionally simple:

```text
46 RM blocks
  first 32 blocks -> HMX matrix multiply
  remaining 14    -> existing fused HVX decode
```

The goal is to establish a measurable HMX implementation before optimizing
data production or layout conversion. This baseline is slower than the HVX
path and is not an exact drop-in replacement on real hardware.

## Matrix Shape

Each RM block expands to one 128-element vector. The first 32 vectors are
packed as columns:

```text
H: 128 x 128 Hadamard generator
X: 128 x 32 expanded RM vectors [v1 v2 ... v32]
Y: 128 x 32 transforms

Y = H * X
```

HMX operates on 32x32 tiles:

```text
H: 4 x 4 WH tiles
X: 4 x 1 AH tiles
Y: 4 x 1 output tiles
```

The generator is prepared once and cached in VTCM as sixteen WH tiles. For
each HMX batch, the decoder packs 32 expanded vectors into AH layout, runs
four output-row tiles, unpacks the results, and uses the existing HVX peak
reduction. It then decodes the 14-block tail with HVX.

The current loop is:

```text
for output_row_tile in 0..3:
    clear_accumulator()
    for k_tile in 0..3:
        load_activation_tile(X[k_tile])
        load_weight_tile(H[output_row_tile][k_tile])
    store_output_tile()
```

## Implemented Changes

### Reed-Muller HMX path

File: `src/ref/reed_muller.c`

- Added `HQC_RM_HMX_BATCH` compile-time gate.
- Added a cached 128x128 Hadamard generator in VTCM.
- Added int16-to-f16 activation packing for 32 RM vectors.
- Added explicit four-tile HMX accumulation per output-row tile.
- Added f16 output unpacking and reuse of the existing HVX peak reduction.
- Kept the remaining RM blocks on the existing two-block HVX path.
- Added separate simulator and real-device initialization paths.
- Fixed the `HQC_RM_HMX_BATCH=0` build so the HVX-only FastRPC control no
  longer includes the simulator-only `h2.h` header.

The H2 simulator readback needs `HQC_RM_HMX_OUTPUT_BIAS=1`. Real-device
HexKL-configured readback uses `HQC_RM_HMX_OUTPUT_BIAS=0`.

### Real-device HMX lifecycle

File: `src/ref/reed_muller.c`

The real-device path now:

1. Sets the compute client class with `HAP_power_set_apptype`.
2. Requests performance DCVS settings.
3. Powers up HVX and HMX.
4. Requests VTCM and one HMX context with `HAP_compute_res`.
5. Acquires VTCM and locks HMX.
6. Calls `hexkl_micro_hmx_setup_acc_read_f16()` for accumulator readback.
7. Unlocks HMX, releases the resource context, and destroys the power
   context after the RPC operation.

File: `../../fastrpc/hqc/dsp/hqc_dsp.c`

- Added `hqc_hmx_begin()` and `hqc_hmx_end()`.
- Wrapped full decode, decode bench, substage bench, and buffer bench RPC
  operations so HMX is acquired once per RPC call rather than once per
  decode.
- Added an RM-byte parity check against the HVX reference before
  Reed-Solomon can mask a wrong RM result.
- Updated RM Hadamard substage benchmarking to use HMX for complete
  32-block chunks and HVX for the tail.

### FastRPC build

File: `../../fastrpc/hqc/build.sh`

- Added `HQC_RM_HMX_BATCH`, `HQC_RM_HMX_DEVICE`, and
  `HQC_RM_HMX_OUTPUT_BIAS` build flags.
- Added `-mhmx` for HMX builds.
- Added `HEXKL_ROOT`, defaulting to `../tools/hexkl-addon`.
- Linked `libhexkl_micro.a` for the real-device HMX variant.

## HexKL Setup

Install the official HexKL addon once:

```sh
bash ../hmx-tutorial/ch05-hmx/install_hexkl.sh
```

The expected default path is:

```text
../tools/hexkl-addon
```

The real-device build checks for:

```text
include/hexkl_micro.h
lib/hexagon_toolv19_v75/libhexkl_micro.a
```

## Build

From the `hqc` repository root:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
HEXAGON_ARCH=v75 \
HQC_PARAM_LEVEL=128 \
HQC_DEFAULT_BENCH_ITERS=125 \
HQC_PROJECT_DIR="$PWD/labs/fastest_hmx" \
HQC_RM_HMX_BATCH=1 \
HQC_RM_HMX_DEVICE=1 \
bash fastrpc/hqc/build_android_gcc_bionic.sh
```

To build the HVX-only FastRPC control from the same source:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
HEXAGON_ARCH=v75 \
HQC_PARAM_LEVEL=128 \
HQC_DEFAULT_BENCH_ITERS=125 \
HQC_PROJECT_DIR="$PWD/labs/fastest_hmx" \
HQC_RM_HMX_BATCH=0 \
HQC_RM_HMX_DEVICE=0 \
bash fastrpc/hqc/build_android_gcc_bionic.sh
```

## Deploy And Run

The HMX baseline was deployed to:

```text
/data/local/tmp/QDC_files/hqc_fastrpc_hmx_baseline
```

Example:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe
REMOTE=/data/local/tmp/QDC_files/hqc_fastrpc_hmx_baseline

"$ADB" shell "mkdir -p $REMOTE"
"$ADB" push "$(wslpath -w "$PWD/fastrpc/hqc/build/hqc_host")" "$REMOTE/hqc_host"
"$ADB" push "$(wslpath -w "$PWD/fastrpc/hqc/build/libhqc_skel.so")" "$REMOTE/libhqc_skel.so"

"$ADB" shell "cd $REMOTE && \
  chmod 755 hqc_host libhqc_skel.so && \
  export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && \
  export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && \
  ./hqc_host bench 10"
```

## Verification

### Simulator smoke test

The explicit tile-loop HMX source passes the H2 simulator decode corpus:

```text
command:
  HQC1_BENCH_ITERS=1 HQC_RM_HMX_BATCH=1 \
  bash labs/fastest_hmx/scripts/run_hqc1_decode_bench_hexagon.sh

result:
  fixtures=256
  total_decodes=256
  result=PASS
  total_pcycles=83,290,128
```

The simulator HMX transform matched the HVX reference after applying the H2
readback bias adjustment.

### Real-device HMX smoke test

Before running the HQC baseline, the tutorial real-device HMX smoke test from
`../ch02-real-device` returned `0`. This validated HMX power-up, VTCM,
resource acquisition, lock, compute, unlock, and release on the connected
Kalama device without hanging cDSP.

### Direct real-device benchmark

Final artifact rebuilt from the default HexKL install path:

| Variant | Command | Decodes | Result | Time per decode |
| --- | --- | ---: | --- | ---: |
| HMX first 32 + HVX tail 14 | `./hqc_host bench 10` | 2,560 | PASS | 356.919 us |
| HVX-only control | `./hqc_host bench 10` | 2,560 | PASS | 49.011 us |

The HMX baseline is currently:

```text
356.919 / 49.011 = 7.282x slower than HVX-only
```

This is expected for the first baseline. Packing expanded vectors into HMX AH
layout remains expensive.

### RM-byte parity check

The final real-device artifact also ran:

```text
./hqc_host substage 2 1
```

Result:

```text
stage=rm_hadamard
total_ops=11776
checksum=0x0000ff8e
result=FAIL
elapsed_ms=203.625
```

This failure is intentional and useful: the substage check compares the full
RM output bytes against the HVX reference before Reed-Solomon can mask an RM
difference.

## Qprof HMX Confirmation

The connected board initially did not contain qprof target binaries. The
target files were installed after `adb root`, `adb remount`, reboot, and a
second remount by following the qprof setup section in `../../run.md`.

Use qprof as a diagnostic tool. It perturbs clocks and should not replace the
direct benchmark above.

Final hybrid capture:

```sh
ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
PROFILE_TIME=15 \
PROFILE_START_DELAY=2 \
scripts/measure_qprof.sh npu1 hqc128_hmx32_hvx14_final \
  'cd /data/local/tmp/QDC_files/hqc_fastrpc_hmx_baseline &&
   chmod +x hqc_host &&
   export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp" &&
   export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic" &&
   ./hqc_host bench 125'
```

Final qprof result:

```text
run_id=20260601_221013_hqc128_hmx32_hvx14_final_npu1
profile_rc=0
workload_rc=0
result=PASS
total_decodes=32000
us_per_decode=353.922
npu_util_percent_avg=99.671950
qdsp_clock_MHz_avg=1476.479158
hmx_util_percent_avg=0.065444
hmx_active_MCPS_avg=0.967538
```

HMX sample breakdown:

| Metric | Samples | Non-zero samples | Average | Maximum |
| --- | ---: | ---: | ---: | ---: |
| `4480` HMX utilization | 2,279 | 1,615 | 0.065444% | 0.098000% |
| `4481` HMX active | 2,279 | 1,615 | 0.967538 MCPS | 1.453000 MCPS |
| `4521` HMX clock | 2,279 | 3 | 0.000652 MHz | 0.495000 MHz |

Local result:

```text
../../results/qprof/qprof_hqc_runs/20260601_221013_hqc128_hmx32_hvx14_final_npu1
```

### HVX-only qprof control

The HVX-only control was profiled separately:

```text
run_id=20260601_220816_hqc128_hvx_control_npu1
profile_rc=0
workload_rc=0
result=PASS
total_decodes=256000
us_per_decode=38.380
```

HMX control breakdown:

| Metric | Samples | Non-zero samples |
| --- | ---: | ---: |
| `4480` HMX utilization | 0 | 0 |
| `4481` HMX active | 139 | 0 |
| `4521` HMX clock | 0 | 0 |

Local result:

```text
../../results/qprof/qprof_hqc_runs/20260601_220816_hqc128_hvx_control_npu1
```

The A/B comparison confirms that the hybrid baseline executes real HMX
instructions. The hybrid workload repeatedly increments HMX utilization and
HMX-active counters; the HVX-only control does not.

## Correctness Status

What is verified:

- H2 simulator decode corpus passes.
- H2 HMX transforms match the HVX reference after simulator bias adjustment.
- Real-device HMX lifecycle works without hanging cDSP.
- Real-device full decode fixtures pass for both `bench 1` and `bench 10`.
- Qprof A/B counters confirm real HMX activity.

What is not verified:

- Real-device RM-byte parity with HVX does not pass.
- Correctness for arbitrary ciphertexts is not established.
- This baseline must not be treated as an exact semantic replacement for the
  HVX decoder.

The observed real-device difference is consistent with approximate f16 HMX
accumulation. The tested full decodes still pass because Reed-Solomon corrects
the resulting RM differences on the deterministic fixture corpus.

## Next Optimization Target

The current bottleneck is not generator construction: the Hadamard generator
is already cached in VTCM. The next useful experiment is to produce the first
32 expanded RM vectors directly in HMX AH layout and avoid:

```text
expanded int16 vectors
  -> f16 conversion
  -> AH tile rearrangement
```

That experiment should retain:

- HVX fallback for the 14-block tail.
- Direct real-device benchmark comparison.
- RM-byte parity diagnostics.
- Qprof A/B confirmation of HMX activity.
