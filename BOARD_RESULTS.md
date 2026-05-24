# Board results

Boards covered:

- Qualcomm Dragonwing RB3 Gen 2 / QCS6490
- Qualcomm Dragonwing QCS8550 / Kalama
- Qualcomm Snapdragon 8 Gen 2 Mobile Hardware Development Kit / HDK8550 / SM8550
- Qualcomm Snapdragon 8 Gen 3 Mobile Reference Design / QRD8650 / SM8650
- OS: Qualcomm Linux
- OS: Android 14
- Runtime path used so far: `/data/local/tmp/QDC_files`

## HQC-128 ARM64 scalar CPU baseline

Folder:

- `scalar_on_board`

Board command:

```sh
cd /data/local/tmp/QDC_files/scalar_on_board
./hqc128_decode_bench_arm64
```

Result:

```text
[arm64-scalar-decode] iters=1000 fixtures=16 total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[arm64-scalar-decode] elapsed_ms=1712.875 ns_per_decode=107054.7 us_per_decode=107.055
```

Baseline:

- ARM64 scalar CPU: `107.055 us/decode`
- Throughput: about `9341 decodes/s`

## HQC-128 FastRPC scalar cDSP baseline

Folder:

- `hqc_fastrpc_scalar`

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_scalar
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host
```

Result:

```text
[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=1000
[fastrpc-scalar-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=7815.520 ns_per_decode=488470.0 us_per_decode=488.470
```

Baseline:

- cDSP scalar via FastRPC: `488.470 us/decode`
- Throughput: about `2047 decodes/s`
- Compared with ARM64 scalar CPU: about `4.56x slower`

## HQC-128 FastRPC HVX intrinsic cDSP

Folder:

- `hqc_fastrpc_intrinsic`

Implementation notes:

- Uses `hqc_lab_insintric` on the DSP side.
- Built with `-mhvx -mhvx-length=128B`.
- Enabled `HQC_USE_HVX_INTRINSICS=1`, `HQC_USE_GF_HWSTYLE_MUL=1`, and `HQC_USE_HVX_RS_SYNDROME=1`.
- Uses `remote_handle64` and opens the target explicitly with `hqc_URI CDSP_DOMAIN`.
- Enables unsigned module loading with `DSPRPC_CONTROL_UNSIGNED_MODULE` before `hqc_open`.

Initial issue:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] FastRPC error=0x2c
```

`0x2c` maps to `AEE_EINVHANDLE`. The same error also reproduced with the
singleton scalar FastRPC path on this board. Switching the intrinsic interface
to explicit `remote_handle64` fixed the cDSP open path.

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 1000
```

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=46.673 ns_per_decode=291708.6 us_per_decode=291.709
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=3684.136 ns_per_decode=230258.5 us_per_decode=230.258
```

Baseline:

- cDSP HVX intrinsic via FastRPC: `230.258 us/decode`
- Throughput: about `4343 decodes/s`
- Compared with cDSP scalar FastRPC: about `2.12x faster`
- Compared with ARM64 scalar CPU: about `2.15x slower`

## HQC-128 FastRPC HVX intrinsic cDSP, opt-in GF LUT

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
HQC_GF_LUT_MUL=1 bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Same FastRPC `remote_handle64` cDSP path as the default intrinsic build.
- Keeps `HQC_USE_HVX_INTRINSICS=1` and `HQC_USE_HVX_RS_SYNDROME=1`.
- Enables `HQC_USE_GF_LUT_MUL=1`.
- Disables the default hardware-style GF multiplier when LUT mode is enabled.
- This is still opt-in because GF multiplication becomes data-dependent table lookup.

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 1000
```

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=35.987 ns_per_decode=224916.6 us_per_decode=224.917
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=2769.751 ns_per_decode=173109.4 us_per_decode=173.109
```

Baseline:

- cDSP HVX intrinsic + GF LUT via FastRPC: `173.109 us/decode`
- Throughput: about `5777 decodes/s`
- Compared with default cDSP HVX intrinsic FastRPC: about `1.33x faster`
- Compared with cDSP scalar FastRPC: about `2.82x faster`
- Compared with ARM64 scalar CPU: about `1.62x slower`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 7 CT default

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Uses the pass 7 default resistance mode from `hqc_lab_insintric`.
- Keeps `HQC_USE_HVX_INTRINSICS=1`, `HQC_USE_GF_HWSTYLE_MUL=1`, and `HQC_USE_HVX_RS_SYNDROME=1`.
- Keeps RS ELP and RS error-value computation in the default fixed-flow mode.
- Uses shortened Chien root search over the public `PARAM_N1 = 46` RS positions.

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 1000
```

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=40.413 ns_per_decode=252578.5 us_per_decode=252.579
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=3133.461 ns_per_decode=195841.3 us_per_decode=195.841
```

Baseline:

- cDSP HVX intrinsic pass 7 CT via FastRPC: `195.841 us/decode`
- Throughput: about `5106 decodes/s`
- Compared with earlier default cDSP HVX intrinsic FastRPC result: about `1.18x faster`
- Compared with cDSP scalar FastRPC: about `2.49x faster`
- Compared with ARM64 scalar CPU: about `1.83x slower`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 7 CT + opt-in GF LUT

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
HQC_GF_LUT_MUL=1 bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Keeps pass 7 default RS resistance mode.
- Enables `HQC_USE_GF_LUT_MUL=1`, so GF multiplication uses data-dependent table lookups.
- This remains an opt-in benchmark path, not the side-channel-resistant default.

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=26.545 ns_per_decode=165904.0 us_per_decode=165.904
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=2119.605 ns_per_decode=132475.3 us_per_decode=132.475
```

Baseline:

- cDSP HVX intrinsic pass 7 CT + GF LUT via FastRPC: `132.475 us/decode`
- Throughput: about `7549 decodes/s`
- Compared with pass 7 CT default GF: about `1.48x faster`
- Compared with cDSP scalar FastRPC: about `3.69x faster`
- Compared with ARM64 scalar CPU: about `1.24x slower`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 7 fast non-CT

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
HQC_RS_FAST_NON_CT=1 bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Enables `HQC_RS_FAST_NON_CT=1`.
- Switches RS ELP to branchy Berlekamp-Massey.
- Switches RS error values to iterate over actual located errors.
- This mode is for throughput comparison only because RS control flow depends on decoded error structure.

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=26.436 ns_per_decode=165222.7 us_per_decode=165.223
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=2106.522 ns_per_decode=131657.6 us_per_decode=131.658
```

Baseline:

- cDSP HVX intrinsic pass 7 fast non-CT via FastRPC: `131.658 us/decode`
- Throughput: about `7595 decodes/s`
- Compared with pass 7 CT default GF: about `1.49x faster`
- Compared with cDSP scalar FastRPC: about `3.71x faster`
- Compared with ARM64 scalar CPU: about `1.23x slower`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 7 fast non-CT + opt-in GF LUT

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
HQC_RS_FAST_NON_CT=1 HQC_GF_LUT_MUL=1 bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Combines branchy RS fast path with GF LUT multiplication.
- This is the fastest measured board path so far.
- It is not a side-channel-resistant configuration because it combines decoded-error-dependent RS control flow with data-dependent GF table lookups.

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=18.947 ns_per_decode=118421.2 us_per_decode=118.421
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=1502.179 ns_per_decode=93886.2 us_per_decode=93.886
```

Baseline:

- cDSP HVX intrinsic pass 7 fast non-CT + GF LUT via FastRPC: `93.886 us/decode`
- Throughput: about `10651 decodes/s`
- Compared with pass 7 fast non-CT default GF: about `1.40x faster`
- Compared with cDSP scalar FastRPC: about `5.20x faster`
- Compared with ARM64 scalar CPU: about `1.14x faster`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 12 fastest benchmark-only path

Date:

- 2026-05-16

Target board:

- Qualcomm Dragonwing RB3 Gen 2 / QCS6490
- Board serial observed during this run: `2890636878`

Folder:

- `hqc_fastrpc_intrinsic`

Build command:

```sh
HQC_RS_FAST_NON_CT=1 \
HQC_GF_LUT_MUL=1 \
HQC_RM_EXPAND_LUT=1 \
HQC_RM_FUSED_FAST=1 \
HQC_RS_ROOTS_HVX=1 \
bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- FastRPC host opens `hqc_URI CDSP_DOMAIN` and links against `libcdsprpc`.
- DSP implementation is in `libhqc_skel.so`, built as `ELF 32-bit ... QUALCOMM DSP6`.
- ARM64 host binary contains the generated FastRPC stub symbol only; `reed_muller_decode` and `reed_solomon_decode` are present in the DSP skel, not in the ARM64 host.
- This is cDSP/HVX execution, not a Qualcomm HTP/NPU path. There is no CPU decoder fallback in this FastRPC host path, and the full benchmark loop runs inside one DSP RPC call.
- This is benchmark-only and not side-channel-resistant: it combines branchy RS control flow, data-dependent GF/RM lookup tables, fused RM, and HVX shortened Chien root search.

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10000
```

Runtime path check:

```text
hqc_fastrpc_intrinsic/build/hqc_host:       ELF 64-bit LSB pie executable, ARM aarch64
hqc_fastrpc_intrinsic/build/libhqc_skel.so: ELF 32-bit LSB shared object, QUALCOMM DSP6
hqc_fastrpc_intrinsic/build/hqc_host:       NEEDED Shared library: [libcdsprpc.so]
hqc_fastrpc_intrinsic/build/libhqc_skel.so:000020e0 T reed_muller_decode
hqc_fastrpc_intrinsic/build/libhqc_skel.so:000025e0 T reed_solomon_decode
```

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=9.692 ns_per_decode=60574.2 us_per_decode=60.574
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=757.965 ns_per_decode=47372.8 us_per_decode=47.373
```

Long benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=7416.581 ns_per_decode=46353.6 us_per_decode=46.354
```

Baseline:

- cDSP HVX intrinsic pass 12 fastest benchmark path via FastRPC: `46.354 us/decode`
- Throughput: about `21573 decodes/s`
- Compared with previous fastest board result, pass 7 fast non-CT + GF LUT: about `2.03x faster`
- Compared with pass 7 CT default GF: about `4.23x faster`
- Compared with cDSP scalar FastRPC: about `10.54x faster`
- Compared with ARM64 scalar CPU: about `2.31x faster`

## HQC-128 FastRPC HVX intrinsic cDSP, pass 13 CT default path

Date:

- 2026-05-17

Target board:

- Qualcomm Dragonwing RB3 Gen 2 / QCS6490
- OS: Qualcomm Linux
- Board serial observed during this run: `2856240174`

Folder:

- `hqc_fastrpc_intrinsic_pass13_ct`

Build command:

```sh
HQC_RS_ROOTS_HVX=1 \
HQC_RM_FUSED_FAST=1 \
bash hqc_fastrpc_intrinsic/build.sh
```

Implementation notes:

- Uses the pass 13 default CT-oriented `hqc_lab_insintric` path.
- Keeps `HQC_USE_HVX_INTRINSICS=1`, `HQC_USE_GF_HWSTYLE_MUL=1`, and `HQC_USE_HVX_RS_SYNDROME=1`.
- Enables default fixed-loop HVX Chien roots with `HQC_RS_ROOTS_HVX=1`.
- Enables arithmetic fused RM with `HQC_RM_FUSED_FAST=1`.
- Leaves `HQC_RS_FAST_NON_CT=0`, `HQC_GF_LUT_MUL=0`, and `HQC_RM_EXPAND_LUT=0`.
- This is the side-channel-resistant default configuration, not the fastest benchmark-only configuration.

Board command:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass13_ct
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10000
```

Runtime path checks:

1. Local artifact check before upload:

```text
hqc_fastrpc_intrinsic/build/hqc_host:       ELF 64-bit LSB pie executable, ARM aarch64
hqc_fastrpc_intrinsic/build/libhqc_skel.so: ELF 32-bit LSB shared object, QUALCOMM DSP6
hqc_host dynamic section: NEEDED Shared library: [libcdsprpc.so]
hqc_host dynamic symbols: U remote_handle64_open
libhqc_skel.so symbols: hqc_decode_bench, hqc_open, reed_muller_decode, reed_solomon_decode
```

The ARM64 host contains the generated FastRPC stub and links against `libcdsprpc`; the decode symbols are in the DSP skel, not in the host binary.

2. Board FastRPC device check:

```text
/dev/fastrpc-cdsp
/dev/fastrpc-cdsp-secure
/dev/fastrpc-adsp-secure
```

The run used `ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"` and `LD_LIBRARY_PATH="$PWD:/usr/lib"`.

3. Negative no-skel test:

```sh
mv libhqc_skel.so libhqc_skel.so.hide
./hqc_host 1
```

Result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1
[fastrpc-intrinsic-decode] hqc_open failed, error=0x80000406
NEGATIVE_RC=1
```

Restoring `libhqc_skel.so` makes the same host pass again. This rules out a silent CPU fallback in this host path: without the DSP skel, the FastRPC open fails instead of running a local CPU decoder.

Smoke result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=22.281 ns_per_decode=139255.4 us_per_decode=139.255
```

Benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1000
[fastrpc-intrinsic-decode] total_decodes=16000 total_rs_symbol_errors=120000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=1770.921 ns_per_decode=110682.5 us_per_decode=110.683
```

Long benchmark result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=17547.744 ns_per_decode=109673.4 us_per_decode=109.673
```

Baseline:

- cDSP HVX intrinsic pass 13 CT default via FastRPC: `109.673 us/decode`
- Throughput: about `9118 decodes/s`
- Compared with pass 7 CT default GF: about `1.79x faster`
- Compared with earlier default cDSP HVX intrinsic FastRPC result: about `2.10x faster`
- Compared with cDSP scalar FastRPC: about `4.45x faster`
- Compared with ARM64 scalar CPU: about `1.02x slower`
- Compared with pass 12 fastest benchmark-only path: about `2.37x slower`

## HQC-128 RB3 Gen 2 whole-board energy estimate

Date:

- 2026-05-17

Target board:

- Qualcomm Dragonwing RB3 Gen 2 / QCS6490
- OS: Qualcomm Linux
- Board serial observed during this run: `561692253`

Measurement method:

- Sensor paths:
  - `/sys/class/power_supply/qcom-battmgr-bat/voltage_now`
  - `/sys/class/power_supply/qcom-battmgr-bat/current_now`
- Sampling interval: `0.1 s`
- Per-sample power formula:

```text
P_sample_W = voltage_now_uV * abs(current_now_uA) / 1e12
```

- Benchmark energy formula:

```text
run_avg_W       = mean(P_sample_W during benchmark)
idle_avg_W      = mean(P_sample_W during an idle window with the same elapsed_s)
delta_W         = run_avg_W - idle_avg_W
delta_energy_J  = delta_W * elapsed_s
uJ_per_decode   = delta_energy_J * 1e6 / total_decodes
```

Caveats:

- These are whole-board delta-energy estimates from the battery manager current sensor, not isolated CPU or cDSP rail readings.
- The value includes host overhead, memory traffic, FastRPC overhead, scheduler effects, and background board activity.
- `power_now` was not used because it appeared constant during load checks; power was computed from `voltage_now` and `current_now`.
- The numbers are best used for same-session relative comparison, not as absolute silicon power.

Runtime path checks:

```text
/dev/fastrpc-cdsp
/dev/fastrpc-cdsp-secure
/dev/fastrpc-adsp-secure
```

The cDSP host artifacts link against `libcdsprpc.so` and import `remote_handle64_open`.
The ARM64 host does not contain `reed_muller_decode` or `reed_solomon_decode`; those decode symbols live in the DSP skel.
The current-session missing-skel negative test was inconclusive because the cDSP module stayed cached after a successful load, so the stronger checks for this session are the FastRPC link/import path plus the DSP6 skel artifact.

Scalar cDSP harness note:

- The old scalar FastRPC harness failed on this board with `FastRPC error=0x2c` (`AEE_EINVHANDLE`) because it used the older non-`remote_handle64` IDL path.
- For this measurement, `hqc_fastrpc_scalar` was aligned with the intrinsic harness:
  - `interface hqc : remote_handle64`
  - explicit `hqc_open(hqc_URI CDSP_DOMAIN, ...)`
  - `DSPRPC_CONTROL_UNSIGNED_MODULE` enabled for `CDSP_DOMAIN_ID`

Measured results:

| Path | Mode | Result | Total decodes | us/decode | run avg W | idle avg W | delta W | delta energy J | uJ/decode |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar | PASS | 160000 | 106.496 | 1.891218521 | 0.846567772 | 1.044650749 | 17.842634793 | 111.516 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 484.567 | 1.064728556 | 0.844270611 | 0.220457945 | 17.111945691 | 106.950 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 109.723 | 1.101504319 | 0.854479203 | 0.247025116 | 4.369874302 | 27.312 |
| cDSP HVX intrinsic | fastest non-CT | PASS | 160000 | 46.341 | 1.189297831 | 0.876775341 | 0.312522490 | 2.372045699 | 14.825 |

Raw benchmark lines:

```text
[arm64-scalar-decode] elapsed_ms=17039.354 ns_per_decode=106496.0 us_per_decode=106.496
[fastrpc-scalar-decode] elapsed_ms=77530.778 ns_per_decode=484567.4 us_per_decode=484.567
[fastrpc-intrinsic-decode] elapsed_ms=17555.632 ns_per_decode=109722.7 us_per_decode=109.723
[fastrpc-intrinsic-decode] elapsed_ms=7414.509 ns_per_decode=46340.7 us_per_decode=46.341
```

Summary:

- cDSP scalar is much slower than ARM64 scalar here (`484.567 us/decode` vs `106.496 us/decode`), but its lower delta board power makes estimated energy per decode similar (`106.950 uJ` vs `111.516 uJ`).
- cDSP HVX pass 13 CT default is about `4.42x` faster than cDSP scalar and about `3.92x` lower estimated energy per decode.
- cDSP HVX fastest non-CT is about `2.37x` faster than pass 13 CT default and about `1.84x` lower estimated energy per decode.
- Fastest non-CT remains benchmark-only because it uses secret-dependent lookup-oriented paths; pass 13 CT default remains the side-channel-oriented default.

## HQC-128 Qualcomm Dragonwing QCS8550 benchmark

Date:

- 2026-05-17

Target board:

- Qualcomm Dragonwing QCS8550 / Kalama
- OS: Qualcomm Linux
- Board serial observed during this run: `1945242134`
- Machine: `QCS_KALAMAP`
- SoC ID: `603`
- Kernel: `Linux kalama 5.15.119-qki-consolidate-android13-8-g04227b5d1a21 #1 SMP PREEMPT Wed Oct 25 02:43:04 UTC 2023 aarch64 GNU/Linux`

Runtime path:

- `/data/local/tmp/QDC_files`

Build and upload notes:

- ARM64 CPU scalar was built with:

```sh
HQC128_BENCH_ITERS=10000 bash scalar_on_board_cpu/build_arm64.sh
```

- FastRPC scalar was built with:

```sh
bash hqc_fastrpc_scalar/build.sh
```

- FastRPC HVX pass 13 CT default was built with:

```sh
HQC_RS_ROOTS_HVX=1 \
HQC_RM_FUSED_FAST=1 \
bash hqc_fastrpc_intrinsic/build.sh
```

- FastRPC HVX pass 12 fastest benchmark-only path was built with:

```sh
HQC_RS_FAST_NON_CT=1 \
HQC_GF_LUT_MUL=1 \
HQC_RM_EXPAND_LUT=1 \
HQC_RM_FUSED_FAST=1 \
HQC_RS_ROOTS_HVX=1 \
bash hqc_fastrpc_intrinsic/build.sh
```

- FastRPC DSP artifacts were built with the script default `HEXAGON_ARCH=v68`.
- Uploaded board folders:
  - `/data/local/tmp/QDC_files/scalar_on_board`
  - `/data/local/tmp/QDC_files/hqc_fastrpc_scalar`
  - `/data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass13_ct`
  - `/data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass12_fastest`

Board commands:

```sh
cd /data/local/tmp/QDC_files/scalar_on_board
./hqc128_decode_bench_arm64

cd /data/local/tmp/QDC_files/hqc_fastrpc_scalar
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass13_ct
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass12_fastest
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10
./hqc_host 10000
```

FastRPC runtime note:

- The QCS8550 run printed `BufferAllocator.cpp:90] Using DMA-BUF heap named: system` for FastRPC host invocations.
- All FastRPC smoke and long runs returned `result=PASS`.
- The path is cDSP FastRPC/HVX, not a CPU fallback: the ARM64 host binary is the FastRPC host/stub and `libhqc_skel.so` is the DSP shared object uploaded beside it.

Measured results:

| Path | Mode | Result | Total decodes | us/decode | Throughput decodes/s |
|---|---|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar | PASS | 160000 | 75.010 | 13332 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 459.237 | 2178 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 106.693 | 9373 |
| cDSP HVX intrinsic | pass 12 fastest non-CT | PASS | 160000 | 45.870 | 21801 |

Raw benchmark lines:

```text
[arm64-scalar-decode] iters=10000 fixtures=16 total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[arm64-scalar-decode] elapsed_ms=12001.663 ns_per_decode=75010.4 us_per_decode=75.010

[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10
[fastrpc-scalar-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=90.073 ns_per_decode=562955.7 us_per_decode=562.956

[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10000
[fastrpc-scalar-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=73477.890 ns_per_decode=459236.8 us_per_decode=459.237

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=22.217 ns_per_decode=138853.8 us_per_decode=138.854

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=17070.891 ns_per_decode=106693.1 us_per_decode=106.693

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=10.381 ns_per_decode=64878.6 us_per_decode=64.879

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=7339.130 ns_per_decode=45869.6 us_per_decode=45.870
```

Summary:

- ARM64 CPU scalar on this QCS8550 board: `75.010 us/decode`, about `13332 decodes/s`.
- cDSP scalar via FastRPC: `459.237 us/decode`, about `6.12x slower` than ARM64 CPU scalar.
- cDSP HVX pass 13 CT default: `106.693 us/decode`, about `4.30x faster` than cDSP scalar and about `1.42x slower` than ARM64 CPU scalar.
- cDSP HVX pass 12 fastest benchmark-only path: `45.870 us/decode`, about `2.33x faster` than pass 13 CT default, `10.01x faster` than cDSP scalar, and `1.64x faster` than ARM64 CPU scalar.
- Compared with the previous QCS6490 long-run results in this file, QCS8550 pass 13 CT default is about `1.03x faster` (`106.693` vs `109.673 us/decode`), and fastest non-CT is about `1.01x faster` (`45.870` vs `46.354 us/decode`) with the current `HEXAGON_ARCH=v68` FastRPC build.

## HQC-128 Qualcomm Dragonwing QCS8550 whole-board energy estimate

Date:

- 2026-05-17

Target board:

- Qualcomm Dragonwing QCS8550 / Kalama
- OS: Qualcomm Linux
- Board serial observed during this run: `1945242134`
- Machine: `QCS_KALAMAP`
- SoC ID: `603`

Measurement method:

- Sensor paths:
  - `/sys/class/power_supply/battery/voltage_now`
  - `/sys/class/power_supply/battery/current_now`
- `power_now` existed at `/sys/class/power_supply/battery/power_now`, but read as `0`, so power was computed from voltage and current.
- Sampling interval: `0.1 s`
- Per-sample power formula:

```text
P_sample_W = voltage_now_uV * abs(current_now_uA) / 1e12
```

- Benchmark energy formula:

```text
run_avg_W       = mean(P_sample_W during benchmark)
idle_avg_W      = mean(P_sample_W during an idle window with the same elapsed_s)
delta_W         = run_avg_W - idle_avg_W
delta_energy_J  = delta_W * elapsed_s
uJ_per_decode   = delta_energy_J * 1e6 / total_decodes
```

Caveats:

- These are whole-board delta-energy estimates from the battery power-supply current sensor, not isolated CPU, cDSP, memory, or PMIC rail measurements.
- The value includes host overhead, memory traffic, FastRPC overhead, scheduler effects, thermal/DVFS state, charging/battery-manager behavior, and background board activity.
- The CPU scalar run shows a much larger measured delta power than the cDSP runs on this board/session, so use the numbers primarily for same-session relative comparison.

Board command pattern:

```sh
export VOLTAGE_PATH=/sys/class/power_supply/battery/voltage_now
export CURRENT_PATH=/sys/class/power_supply/battery/current_now
export SAMPLE_INTERVAL=0.1

cd /data/local/tmp/QDC_files/scalar_on_board
/data/local/tmp/QDC_files/measure_board_energy.sh qcs8550_arm64_scalar ./hqc128_decode_bench_arm64

cd /data/local/tmp/QDC_files/hqc_fastrpc_scalar
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
/data/local/tmp/QDC_files/measure_board_energy.sh qcs8550_cdsp_scalar ./hqc_host 10000

cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass13_ct
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
/data/local/tmp/QDC_files/measure_board_energy.sh qcs8550_cdsp_hvx_pass13_ct ./hqc_host 10000

cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic_pass12_fastest
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
/data/local/tmp/QDC_files/measure_board_energy.sh qcs8550_cdsp_hvx_pass12_fastest ./hqc_host 10000
```

Measured results:

| Path | Mode | Result | Total decodes | us/decode | run avg W | idle avg W | delta W | delta energy J | uJ/decode |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar | PASS | 160000 | 76.997 | 7.497060633 | 2.662360097 | 4.834700536 | 60.433756700 | 377.711 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 459.439 | 3.059173549 | 2.649881656 | 0.409291893 | 30.177091271 | 188.607 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 106.697 | 3.072115608 | 2.644530809 | 0.427584799 | 7.397217023 | 46.233 |
| cDSP HVX intrinsic | pass 12 fastest non-CT | PASS | 160000 | 45.864 | 3.257579232 | 2.616769941 | 0.640809291 | 4.825293961 | 30.158 |

Raw energy result lines:

```text
[energy-result] label=qcs8550_arm64_scalar rc=0 result=PASS elapsed_s=12.500000 total_decodes=160000 us_per_decode=76.997 run_avg_W=7.497060633 idle_avg_W=2.662360097 delta_W=4.834700536 delta_energy_J=60.433756700 uJ_per_decode=377.711 run_samples=72 idle_samples=50
[energy-result] label=qcs8550_cdsp_scalar rc=0 result=PASS elapsed_s=73.730000 total_decodes=160000 us_per_decode=459.439 run_avg_W=3.059173549 idle_avg_W=2.649881656 delta_W=0.409291893 delta_energy_J=30.177091271 uJ_per_decode=188.607 run_samples=376 idle_samples=285
[energy-result] label=qcs8550_cdsp_hvx_pass13_ct rc=0 result=PASS elapsed_s=17.300000 total_decodes=160000 us_per_decode=106.697 run_avg_W=3.072115608 idle_avg_W=2.644530809 delta_W=0.427584799 delta_energy_J=7.397217023 uJ_per_decode=46.233 run_samples=89 idle_samples=68
[energy-result] label=qcs8550_cdsp_hvx_pass12_fastest rc=0 result=PASS elapsed_s=7.530000 total_decodes=160000 us_per_decode=45.864 run_avg_W=3.257579232 idle_avg_W=2.616769941 delta_W=0.640809291 delta_energy_J=4.825293961 uJ_per_decode=30.158 run_samples=39 idle_samples=30
```

Raw benchmark lines from the energy-wrapped runs:

```text
[arm64-scalar-decode] elapsed_ms=12319.459 ns_per_decode=76996.6 us_per_decode=76.997
[fastrpc-scalar-decode] elapsed_ms=73510.234 ns_per_decode=459439.0 us_per_decode=459.439
[fastrpc-intrinsic-decode] elapsed_ms=17071.578 ns_per_decode=106697.4 us_per_decode=106.697
[fastrpc-intrinsic-decode] elapsed_ms=7338.247 ns_per_decode=45864.0 us_per_decode=45.864
```

Summary:

- ARM64 CPU scalar estimated whole-board delta energy: `377.711 uJ/decode`.
- cDSP scalar estimated whole-board delta energy: `188.607 uJ/decode`, about `2.00x` lower than ARM64 CPU scalar despite being much slower.
- cDSP HVX pass 13 CT default estimated whole-board delta energy: `46.233 uJ/decode`, about `4.08x` lower than cDSP scalar and `8.17x` lower than ARM64 CPU scalar.
- cDSP HVX pass 12 fastest benchmark-only path estimated whole-board delta energy: `30.158 uJ/decode`, about `1.53x` lower than pass 13 CT default, `6.25x` lower than cDSP scalar, and `12.52x` lower than ARM64 CPU scalar.

## HQC-128 Snapdragon 8 Gen 3 QRD8650 Android benchmark

Date:

- 2026-05-17

Target board:

- Qualcomm Snapdragon 8 Gen 3 Mobile Reference Design / QRD8650
- Android product board: `pineapple`
- Android model: `Pineapple for arm64`
- SoC model: `SM8650`
- SoC ID: `557`
- Machine: `PINEAPPLE`
- Board serial observed during this run: `1285355612`
- Kernel: `Linux localhost 6.1.57-android14-11-maybe-dirty #1 SMP PREEMPT Thu Jan 1 00:00:00 UTC 1970 aarch64 Toybox`
- ADB transport: `45207755`

Runtime path:

- `/data/local/tmp/QDC_files`

Build and upload notes:

- ARM64 CPU scalar was built as a static Linux/AArch64 binary so it can run directly under the Android kernel:

```sh
aarch64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -static \
    -ffunction-sections -fdata-sections \
    -DHQC128_BENCH_ITERS=10000 \
    -I hqc_lab_scalar/fixtures \
    -I hqc_lab_scalar/src/common \
    -I hqc_lab_scalar/src/ref \
    -I hqc_lab_scalar/src/ref/hqc-1 \
    scalar_on_board_cpu/hqc128_decode_bench_arm64.c \
    hqc_lab_scalar/fixtures/hqc128_decode_fixture.c \
    hqc_lab_scalar/src/common/fft.c \
    hqc_lab_scalar/src/ref/gf.c \
    hqc_lab_scalar/src/ref/reed_muller.c \
    hqc_lab_scalar/src/ref/reed_solomon.c \
    -Wl,--gc-sections \
    -o /tmp/hqc128_decode_bench_arm64_static
```

- Android did not have an NDK in this workspace. The FastRPC host was built by compiling AArch64 objects with `aarch64-linux-gnu-gcc`, then linking a PIE against Bionic libraries pulled from the device:
  - `/apex/com.android.runtime/lib64/bionic/libc.so`
  - `/apex/com.android.runtime/lib64/bionic/libdl.so`
  - `/apex/com.android.runtime/lib64/bionic/libm.so`
  - `/system/lib64/liblog.so`
  - `/system/lib64/ld-android.so`
  - `/vendor/lib64/libcdsprpc.so`
- The custom Android host uses interpreter `/system/bin/linker64` and RUNPATH `/apex/com.android.runtime/lib64/bionic:/system/lib64:/vendor/lib64`.
- A small custom `_start` calls `main` and then Bionic `exit()` so benchmark output flushes correctly.
- DSP skels were the same Hexagon `v68` FastRPC artifacts used for the QCS8550 run.

FastRPC runtime checks:

- ADB shell was root: `uid=0(root) ... context=u:r:su:s0`
- FastRPC/DSP device nodes observed:

```text
/dev/adsprpc-smd
/dev/adsprpc-smd-secure
/dev/glink_pkt_ctrl_cdsp
/dev/glink_pkt_data_cdsp
/dev/rdbg_cdsp
/dev/remoteproc-cdsp-md
```

- Device library paths observed:

```text
/vendor/lib64/libcdsprpc.so
/vendor/lib/rfsa/adsp
```

Board commands:

```sh
cd /data/local/tmp/QDC_files/sm8650_cpu
./hqc128_decode_bench_arm64_static

cd /data/local/tmp/QDC_files/sm8650_fastrpc_scalar
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/sm8650_fastrpc_intrinsic_pass13_ct
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/sm8650_fastrpc_intrinsic_pass12_fastest
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000
```

Smoke results:

```text
[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10
[fastrpc-scalar-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=83.755 ns_per_decode=523465.8 us_per_decode=523.466

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=21.036 ns_per_decode=131473.0 us_per_decode=131.473

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=9.385 ns_per_decode=58654.9 us_per_decode=58.655
```

Measured long-run results:

| Path | Mode | Result | Total decodes | us/decode | Throughput decodes/s |
|---|---|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar static | PASS | 160000 | 71.084 | 14068 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 366.321 | 2730 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 85.642 | 11677 |
| cDSP HVX intrinsic | pass 12 fastest non-CT | PASS | 160000 | 36.897 | 27102 |

Raw long-run benchmark lines:

```text
[arm64-scalar-decode] iters=10000 fixtures=16 total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[arm64-scalar-decode] elapsed_ms=11373.491 ns_per_decode=71084.3 us_per_decode=71.084

[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10000
[fastrpc-scalar-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=58611.282 ns_per_decode=366320.5 us_per_decode=366.321

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=13702.670 ns_per_decode=85641.7 us_per_decode=85.642

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=5903.491 ns_per_decode=36896.8 us_per_decode=36.897
```

Summary:

- ARM64 CPU scalar on SM8650 Android: `71.084 us/decode`, about `14068 decodes/s`.
- cDSP scalar via FastRPC: `366.321 us/decode`, about `5.15x slower` than ARM64 CPU scalar.
- cDSP HVX pass 13 CT default: `85.642 us/decode`, about `4.28x faster` than cDSP scalar and about `1.20x slower` than ARM64 CPU scalar.
- cDSP HVX pass 12 fastest benchmark-only path: `36.897 us/decode`, about `2.32x faster` than pass 13 CT default, `9.93x faster` than cDSP scalar, and `1.93x faster` than ARM64 CPU scalar.
- Compared with QCS8550/Kalama long-run results in this file, SM8650 is about `1.06x` faster for ARM64 CPU scalar, `1.25x` faster for cDSP scalar, `1.25x` faster for pass 13 CT default, and `1.24x` faster for the fastest non-CT path.

## HQC-128 Snapdragon 8 Gen 3 QRD8650 Android energy sensor attempt

Date:

- 2026-05-17

Target board:

- Qualcomm Snapdragon 8 Gen 3 Mobile Reference Design / QRD8650
- SoC model: `SM8650`
- SoC ID: `557`

Measurement method attempted:

- Sensor paths:
  - `/sys/class/power_supply/battery/voltage_now`
  - `/sys/class/power_supply/battery/current_now`
- `power_now` existed at `/sys/class/power_supply/battery/power_now`, but read as `0`.
- Sampling interval: `0.1 s`
- Per-sample formula used:

```text
P_sample_W = voltage_now_uV * abs(current_now_uA) / 1e12
```

Important caveat:

- These QRD8650 Android battery sensor readings are not usable for meaningful energy/decode conclusions in this session.
- The reported battery current was only around a few thousand units while the battery voltage was about `8.0 V`, producing tiny power values around `0.02-0.04 W`.
- Two cDSP runs produced negative delta power because the post-run idle window averaged higher than the run window.
- Therefore, the latency results above should be treated as valid, but the energy numbers below should be kept only as raw sensor evidence, not as benchmark conclusions.

Raw power-supply candidates:

```text
/sys/class/power_supply/battery/voltage_now=8015058
/sys/class/power_supply/ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1/voltage_now=5000000
/sys/class/power_supply/usb/voltage_now=4995000
/sys/class/power_supply/wireless/voltage_now=8000
/sys/class/power_supply/battery/current_now=-5493
/sys/class/power_supply/ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1/current_now=0
/sys/class/power_supply/usb/current_now=0
/sys/class/power_supply/wireless/current_now=0
/sys/class/power_supply/battery/power_now=0
```

Raw energy result lines:

```text
[energy-result] label=sm8650_arm64_scalar rc=0 result=PASS elapsed_s=11.460000 total_decodes=160000 us_per_decode=71.084 run_avg_W=0.030296512 idle_avg_W=0.026596365 delta_W=0.003700147 delta_energy_J=0.042403685 uJ_per_decode=0.265 run_samples=68 idle_samples=46
[energy-result] label=sm8650_cdsp_scalar rc=0 result=PASS elapsed_s=58.860000 total_decodes=160000 us_per_decode=366.321 run_avg_W=0.031644788 idle_avg_W=0.038361979 delta_W=-0.006717191 delta_energy_J=-0.395373862 uJ_per_decode=-2.471 run_samples=286 idle_samples=232
[energy-result] label=sm8650_cdsp_hvx_pass13_ct rc=0 result=PASS elapsed_s=13.900000 total_decodes=160000 us_per_decode=85.642 run_avg_W=0.032009583 idle_avg_W=0.024626488 delta_W=0.007383095 delta_energy_J=0.102625021 uJ_per_decode=0.641 run_samples=67 idle_samples=56
[energy-result] label=sm8650_cdsp_hvx_pass12_fastest rc=0 result=PASS elapsed_s=6.180000 total_decodes=160000 us_per_decode=36.897 run_avg_W=0.024151980 idle_avg_W=0.030516746 delta_W=-0.006364766 delta_energy_J=-0.039334254 uJ_per_decode=-0.246 run_samples=30 idle_samples=25
```

## HQC-128 Snapdragon 8 Gen 2 HDK8550 Android benchmark and energy estimate

Date:

- 2026-05-17

Target board:

- Qualcomm Snapdragon 8 Gen 2 Mobile Hardware Development Kit / HDK8550
- Android product board: `kalama`
- Android model: `Kalama for arm64`
- SoC model: `SM8550`
- SoC ID: `536`
- Machine: `KALAMAP`
- Board serial observed during this run: `1504493079`
- Kernel: `Linux localhost 5.15.149-android13-8-gdf9c354e3325 #1 SMP PREEMPT Thu Sep 5 06:44:44 UTC 2024 aarch64 Toybox`
- ADB transport: `b9d50bd8`

Runtime path:

- `/data/local/tmp/QDC_files`

Build and upload notes:

- ARM64 CPU scalar used the same static Linux/AArch64 binary as the QRD8650 run.
- Android FastRPC host used the same Bionic PIE host build as the QRD8650 run:
  - interpreter `/system/bin/linker64`
  - RUNPATH `/apex/com.android.runtime/lib64/bionic:/system/lib64:/vendor/lib64`
  - NEEDED `libcdsprpc.so` and `libc.so`
- DSP skels were the same Hexagon `v68` FastRPC artifacts used for QCS8550 and QRD8650.

FastRPC runtime checks:

- ADB shell was root: `uid=0(root) ... context=u:r:su:s0`
- FastRPC/DSP device nodes observed:

```text
/dev/adsprpc-smd
/dev/adsprpc-smd-secure
/dev/rdbg_cdsp
/dev/remoteproc-cdsp-md
```

- Device library paths observed:

```text
/vendor/lib64/libcdsprpc.so
/vendor/lib/rfsa/adsp
```

- No-skel negative test:

```sh
cd /data/local/tmp/QDC_files/hdk8550_noskel_test
export ADSP_LIBRARY_PATH="$PWD"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 1
```

Result:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=1
[fastrpc-intrinsic-decode] hqc_open failed, error=0x80000406
```

This rules out a silent CPU fallback in this host path: without `libhqc_skel.so`, FastRPC open fails instead of running a local decoder.

Board commands:

```sh
cd /data/local/tmp/QDC_files/hdk8550_cpu
./hqc128_decode_bench_arm64_static

cd /data/local/tmp/QDC_files/hdk8550_fastrpc_scalar
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/hdk8550_fastrpc_intrinsic_pass13_ct
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000

cd /data/local/tmp/QDC_files/hdk8550_fastrpc_intrinsic_pass12_fastest
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 10
./hqc_host 10000
```

Smoke results:

```text
[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10
[fastrpc-scalar-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=88.720 ns_per_decode=554497.1 us_per_decode=554.497

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=22.347 ns_per_decode=139668.0 us_per_decode=139.668

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10
[fastrpc-intrinsic-decode] total_decodes=160 total_rs_symbol_errors=1200 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=10.446 ns_per_decode=65284.8 us_per_decode=65.285
```

Measured long-run results:

| Path | Mode | Result | Total decodes | us/decode | Throughput decodes/s |
|---|---|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar static | PASS | 160000 | 79.656 | 12554 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 459.659 | 2176 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 106.655 | 9376 |
| cDSP HVX intrinsic | pass 12 fastest non-CT | PASS | 160000 | 45.830 | 21820 |

Raw long-run benchmark lines:

```text
[arm64-scalar-decode] iters=10000 fixtures=16 total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[arm64-scalar-decode] elapsed_ms=12744.902 ns_per_decode=79655.6 us_per_decode=79.656

[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10000
[fastrpc-scalar-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=73545.444 ns_per_decode=459659.0 us_per_decode=459.659

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=17064.778 ns_per_decode=106654.9 us_per_decode=106.655

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=7332.833 ns_per_decode=45830.2 us_per_decode=45.830
```

Summary:

- ARM64 CPU scalar on HDK8550 Android: `79.656 us/decode`, about `12554 decodes/s`.
- cDSP scalar via FastRPC: `459.659 us/decode`, about `5.77x slower` than ARM64 CPU scalar.
- cDSP HVX pass 13 CT default: `106.655 us/decode`, about `4.31x faster` than cDSP scalar and about `1.34x slower` than ARM64 CPU scalar.
- cDSP HVX pass 12 fastest benchmark-only path: `45.830 us/decode`, about `2.33x faster` than pass 13 CT default, `10.03x faster` than cDSP scalar, and `1.74x faster` than ARM64 CPU scalar.
- Compared with QCS8550/Kalama long-run results in this file, HDK8550 is about `0.94x` as fast for ARM64 CPU scalar, and essentially equal for cDSP scalar, pass 13 CT default, and fastest non-CT.
- Compared with QRD8650/SM8650 long-run results in this file, HDK8550 is about `1.12x` slower for ARM64 CPU scalar, `1.25x` slower for cDSP scalar, `1.25x` slower for pass 13 CT default, and `1.24x` slower for fastest non-CT.

Energy measurement method:

- Sensor paths:
  - `/sys/class/power_supply/battery/voltage_now`
  - `/sys/class/power_supply/battery/current_now`
- `power_now` existed at `/sys/class/power_supply/battery/power_now`, but read as `0`.
- Sampling interval: `0.1 s`
- Per-sample formula used:

```text
P_sample_W = voltage_now_uV * abs(current_now_uA) / 1e12
```

Raw power-supply candidates:

```text
/sys/class/power_supply/battery/voltage_now=4327403
/sys/class/power_supply/ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1/voltage_now=5000000
/sys/class/power_supply/usb/voltage_now=4956000
/sys/class/power_supply/wireless/voltage_now=5000
/sys/class/power_supply/battery/current_now=-159671
/sys/class/power_supply/ucsi-source-psy-soc:qcom,pmic_glink:qcom,ucsi1/current_now=0
/sys/class/power_supply/usb/current_now=0
/sys/class/power_supply/wireless/current_now=0
/sys/class/power_supply/battery/power_now=0
```

Measured energy results:

| Path | Mode | Result | Total decodes | us/decode | run avg W | idle avg W | delta W | delta energy J | uJ/decode |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| ARM64 CPU scalar | CPU scalar static | PASS | 160000 | 79.656 | 3.077484027 | 0.695572757 | 2.381911270 | 30.512283369 | 190.702 |
| cDSP scalar | FastRPC scalar | PASS | 160000 | 459.659 | 0.951926834 | 0.690142580 | 0.261784254 | 19.327531473 | 120.797 |
| cDSP HVX intrinsic | pass 13 CT default | PASS | 160000 | 106.655 | 0.973194463 | 0.660496946 | 0.312697517 | 5.409667044 | 33.810 |
| cDSP HVX intrinsic | pass 12 fastest non-CT | PASS | 160000 | 45.830 | 1.070237714 | 0.624185203 | 0.446052511 | 3.363235933 | 21.020 |

Raw energy result lines:

```text
[energy-result] label=hdk8550_arm64_scalar rc=0 result=PASS elapsed_s=12.810000 total_decodes=160000 us_per_decode=79.656 run_avg_W=3.077484027 idle_avg_W=0.695572757 delta_W=2.381911270 delta_energy_J=30.512283369 uJ_per_decode=190.702 run_samples=75 idle_samples=56
[energy-result] label=hdk8550_cdsp_scalar rc=0 result=PASS elapsed_s=73.830000 total_decodes=160000 us_per_decode=459.659 run_avg_W=0.951926834 idle_avg_W=0.690142580 delta_W=0.261784254 delta_energy_J=19.327531473 uJ_per_decode=120.797 run_samples=365 idle_samples=318
[energy-result] label=hdk8550_cdsp_hvx_pass13_ct rc=0 result=PASS elapsed_s=17.300000 total_decodes=160000 us_per_decode=106.655 run_avg_W=0.973194463 idle_avg_W=0.660496946 delta_W=0.312697517 delta_energy_J=5.409667044 uJ_per_decode=33.810 run_samples=85 idle_samples=75
[energy-result] label=hdk8550_cdsp_hvx_pass12_fastest rc=0 result=PASS elapsed_s=7.540000 total_decodes=160000 us_per_decode=45.830 run_avg_W=1.070237714 idle_avg_W=0.624185203 delta_W=0.446052511 delta_energy_J=3.363235933 uJ_per_decode=21.020 run_samples=37 idle_samples=33
```

Energy summary:

- ARM64 CPU scalar estimated whole-board delta energy: `190.702 uJ/decode`.
- cDSP scalar estimated whole-board delta energy: `120.797 uJ/decode`, about `1.58x` lower than ARM64 CPU scalar despite being slower.
- cDSP HVX pass 13 CT default estimated whole-board delta energy: `33.810 uJ/decode`, about `3.57x` lower than cDSP scalar and `5.64x` lower than ARM64 CPU scalar.
- cDSP HVX pass 12 fastest benchmark-only path estimated whole-board delta energy: `21.020 uJ/decode`, about `1.61x` lower than pass 13 CT default and `9.07x` lower than ARM64 CPU scalar.
