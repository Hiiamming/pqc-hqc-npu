# Board results

Target board:

- Qualcomm Dragonwing RB3 Gen 2 / QCS6490
- OS: Qualcomm Linux
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
