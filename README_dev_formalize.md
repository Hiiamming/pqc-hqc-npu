# HQC-128 RB3 Gen 2 Board Benchmark Formalization

## Experimental Setting

- Date: 2026-05-17.
- Board: Qualcomm Dragonwing RB3 Gen 2 / QCS6490 Linux.
- Kernel: `6.6.97-qli-1.6-ver.1.2.1-05029-g53c11c30e98d-dirty`.
- Benchmark corpus: 16 deterministic HQC-128 decode fixtures per iteration.
- Main board runs: `iters=10000`, so `160000` total decodes per variant.
- Metric: host-observed wall-clock latency from `clock_gettime(CLOCK_MONOTONIC)`.

## Variant Definitions

| Variant | Path | Runs on | Notes |
| --- | --- | --- | --- |
| `cpu_scalar_arm64` | `scalar_on_board_cpu` | ARM64 CPU | Direct process, no FastRPC/cDSP/HVX |
| `cdsp_scalar_fastrpc` | `hqc_fastrpc_scalar` | cDSP through FastRPC | Scalar DSP baseline |
| `cdsp_intrinsic_default_ct` | `hqc_fastrpc_intrinsic` | cDSP through FastRPC | CT-oriented HVX path, built with `HQC_RS_ROOTS_HVX=1`, `HQC_RM_FUSED_FAST=1` |
| `cdsp_intrinsic_fastest_non_ct` | `hqc_fastrpc_intrinsic` | cDSP through FastRPC | Benchmark-only path with `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, `HQC_RM_FUSED_FAST=1`, `HQC_RS_ROOTS_HVX=1` |

## Main Board Results

| Variant | Iters | Total decodes | Result | Elapsed ms | us/decode | Throughput decodes/s | Speedup vs CPU scalar | Speedup vs cDSP scalar |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| `cpu_scalar_arm64` | 10000 | 160000 | PASS | 17,036.715 | 106.479 | 9,391.5 | 1.00x | 4.55x |
| `cdsp_scalar_fastrpc` | 10000 | 160000 | PASS | 77,528.601 | 484.554 | 2,063.8 | 0.22x | 1.00x |
| `cdsp_intrinsic_default_ct` | 10000 | 160000 | PASS | 17,745.354 | 110.908 | 9,016.5 | 0.96x | 4.37x |
| `cdsp_intrinsic_fastest_non_ct` | 10000 | 160000 | PASS | 7,599.123 | 47.495 | 21,054.8 | 2.24x | 10.20x |

## FastRPC Overhead Check

The cDSP benchmark performs one FastRPC call for the whole decode loop, not one FastRPC call per decode. Small `iters=1` runs include a visible fixed-call/cold-start component. The main result therefore uses `iters=10000`, where this fixed cost is amortized.

| Variant | Iters | Total decodes | Result | Elapsed ms | us/decode |
| --- | ---: | ---: | --- | ---: | ---: |
| `cpu_scalar_arm64` | 1000 | 16000 | PASS | 1,743.420 | 108.964 |
| `cdsp_scalar_fastrpc` | 1 | 16 | PASS | 9.890 | 618.096 |
| `cdsp_scalar_fastrpc` | 1000 | 16000 | PASS | 7,772.016 | 485.751 |
| `cdsp_intrinsic_default_ct` | 1 | 16 | PASS | 2.697 | 168.585 |
| `cdsp_intrinsic_default_ct` | 1000 | 16000 | PASS | 1,799.138 | 112.446 |
| `cdsp_intrinsic_fastest_non_ct` | 1 | 16 | PASS | 2.278 | 142.352 |
| `cdsp_intrinsic_fastest_non_ct` | 1000 | 16000 | PASS | 785.692 | 49.106 |

## Observations

- The ARM64 CPU scalar baseline is faster than the cDSP scalar FastRPC path on this benchmark: `106.479 us/decode` vs `484.554 us/decode`.
- The cDSP intrinsic default CT path is close to CPU scalar on wall-clock latency: `110.908 us/decode`, about `0.96x` CPU scalar speed.
- The cDSP fastest non-CT path is the fastest real-device run here: `47.495 us/decode`, about `2.24x` faster than CPU scalar and `2.34x` faster than cDSP intrinsic default CT.
- The fastest non-CT path remains benchmark-only because it uses side-channel-relaxed RS control flow and data-dependent lookup tables.

## Machine-Readable CSV

```csv
variant,iters,total_decodes,result,elapsed_ms,us_per_decode,throughput_decodes_per_s,speedup_vs_cpu_scalar,speedup_vs_cdsp_scalar
cpu_scalar_arm64,10000,160000,PASS,17036.715,106.479,9391.5,1.00,4.55
cdsp_scalar_fastrpc,10000,160000,PASS,77528.601,484.554,2063.8,0.22,1.00
cdsp_intrinsic_default_ct,10000,160000,PASS,17745.354,110.908,9016.5,0.96,4.37
cdsp_intrinsic_fastest_non_ct,10000,160000,PASS,7599.123,47.495,21054.8,2.24,10.20
```

```csv
variant,iters,total_decodes,result,elapsed_ms,us_per_decode
cpu_scalar_arm64,1000,16000,PASS,1743.420,108.964
cdsp_scalar_fastrpc,1,16,PASS,9.890,618.096
cdsp_scalar_fastrpc,1000,16000,PASS,7772.016,485.751
cdsp_intrinsic_default_ct,1,16,PASS,2.697,168.585
cdsp_intrinsic_default_ct,1000,16000,PASS,1799.138,112.446
cdsp_intrinsic_fastest_non_ct,1,16,PASS,2.278,142.352
cdsp_intrinsic_fastest_non_ct,1000,16000,PASS,785.692,49.106
```

# HQC-128 QRD8650 Android Benchmark Formalization

## Experimental Setting

- Date: 2026-05-17.
- Board: Snapdragon 8 Gen 3 Mobile Reference Design / QRD8650 Android.
- Android board/model: `pineapple` / `Pineapple for arm64`.
- SoC: `SM8650`, SoC ID `557`, manufacturer `QTI`.
- Board serial observed during this run: `660419645`.
- Kernel: `Linux localhost 6.1.57-android14-11-maybe-dirty #1 SMP PREEMPT Thu Jan 1 00:00:00 UTC 1970 aarch64 Toybox`.
- Runtime path: `/data/local/tmp/QDC_files/sm8650_*`.
- Benchmark corpus: 16 deterministic HQC-128 decode fixtures per iteration.
- Main board runs: `iters=10000`, so `160000` total decodes per variant.
- Metric: host-observed wall-clock latency from the benchmark process.

## Variant Definitions

| Variant | Path | Runs on | Notes |
| --- | --- | --- | --- |
| `qrd8650_cpu_scalar_arm64` | `sm8650_cpu` | ARM64 CPU | Static Linux/AArch64 scalar binary running under Android kernel |
| `qrd8650_cdsp_scalar_fastrpc` | `sm8650_fastrpc_scalar` | cDSP through FastRPC | Scalar DSP baseline |
| `qrd8650_cdsp_intrinsic_default_ct` | `sm8650_fastrpc_intrinsic_pass13_ct` | cDSP through FastRPC | CT-oriented HVX path, built with `HQC_RS_ROOTS_HVX=1`, `HQC_RM_FUSED_FAST=1` |
| `qrd8650_cdsp_intrinsic_fastest_non_ct` | `sm8650_fastrpc_intrinsic_pass12_fastest` | cDSP through FastRPC | Benchmark-only path with `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, `HQC_RM_FUSED_FAST=1`, `HQC_RS_ROOTS_HVX=1` |

## Android FastRPC Setup

- The CPU scalar binary was built static with `aarch64-linux-gnu-gcc`.
- Android FastRPC hosts were Bionic PIE binaries using interpreter `/system/bin/linker64`.
- Host RUNPATH: `/apex/com.android.runtime/lib64/bionic:/system/lib64:/vendor/lib64`.
- Runtime environment for each FastRPC run:

```sh
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
```

## Smoke Results

| Variant | Iters | Total decodes | Result | Elapsed ms | us/decode |
| --- | ---: | ---: | --- | ---: | ---: |
| `qrd8650_cdsp_scalar_fastrpc` | 10 | 160 | PASS | 84.380 | 527.377 |
| `qrd8650_cdsp_intrinsic_default_ct` | 10 | 160 | PASS | 20.865 | 130.407 |
| `qrd8650_cdsp_intrinsic_fastest_non_ct` | 10 | 160 | PASS | 9.584 | 59.897 |

## Main Board Results

| Variant | Iters | Total decodes | Result | Elapsed ms | us/decode | Throughput decodes/s | Speedup vs CPU scalar | Speedup vs cDSP scalar |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| `qrd8650_cpu_scalar_arm64` | 10000 | 160000 | PASS | 11,355.315 | 70.971 | 14,090.3 | 1.00x | 5.20x |
| `qrd8650_cdsp_scalar_fastrpc` | 10000 | 160000 | PASS | 59,041.120 | 369.007 | 2,710.0 | 0.19x | 1.00x |
| `qrd8650_cdsp_intrinsic_default_ct` | 10000 | 160000 | PASS | 13,861.353 | 86.633 | 11,542.9 | 0.82x | 4.26x |
| `qrd8650_cdsp_intrinsic_fastest_non_ct` | 10000 | 160000 | PASS | 5,992.953 | 37.456 | 26,698.0 | 1.89x | 9.85x |

## Raw Long-Run Lines

```text
[arm64-scalar-decode] iters=10000 fixtures=16 total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[arm64-scalar-decode] elapsed_ms=11355.315 ns_per_decode=70970.7 us_per_decode=70.971

[fastrpc-scalar-decode] calling cDSP scalar decoder, iters=10000
[fastrpc-scalar-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-scalar-decode] elapsed_ms=59041.120 ns_per_decode=369007.0 us_per_decode=369.007

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=13861.353 ns_per_decode=86633.5 us_per_decode=86.633

[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 total_rs_symbol_errors=1200000 checksum=0x00000000 result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=5992.953 ns_per_decode=37456.0 us_per_decode=37.456
```

## Observations

- ARM64 CPU scalar is faster than cDSP scalar FastRPC on QRD8650 Android: `70.971 us/decode` vs `369.007 us/decode`.
- cDSP intrinsic default CT is still slower than CPU scalar on wall-clock latency: `86.633 us/decode`, about `0.82x` CPU scalar speed.
- cDSP intrinsic fastest non-CT is the fastest QRD8650 run: `37.456 us/decode`, about `1.89x` faster than CPU scalar and `2.31x` faster than cDSP intrinsic default CT.
- Compared with the scalar cDSP baseline, default CT HVX is `4.26x` faster and fastest non-CT HVX is `9.85x` faster.
- The fastest non-CT path remains benchmark-only because it uses side-channel-relaxed RS control flow and data-dependent lookup tables.

## Machine-Readable CSV

```csv
variant,iters,total_decodes,result,elapsed_ms,us_per_decode,throughput_decodes_per_s,speedup_vs_cpu_scalar,speedup_vs_cdsp_scalar
qrd8650_cpu_scalar_arm64,10000,160000,PASS,11355.315,70.971,14090.3,1.00,5.20
qrd8650_cdsp_scalar_fastrpc,10000,160000,PASS,59041.120,369.007,2710.0,0.19,1.00
qrd8650_cdsp_intrinsic_default_ct,10000,160000,PASS,13861.353,86.633,11542.9,0.82,4.26
qrd8650_cdsp_intrinsic_fastest_non_ct,10000,160000,PASS,5992.953,37.456,26698.0,1.89,9.85
```

```csv
variant,iters,total_decodes,result,elapsed_ms,us_per_decode
qrd8650_cdsp_scalar_fastrpc,10,160,PASS,84.380,527.377
qrd8650_cdsp_intrinsic_default_ct,10,160,PASS,20.865,130.407
qrd8650_cdsp_intrinsic_fastest_non_ct,10,160,PASS,9.584,59.897
```
