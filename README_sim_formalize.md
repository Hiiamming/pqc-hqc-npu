# HQC-128 Decode Benchmark Formalization

## Experimental Setting

- Date: 2026-05-17.
- Platform: Hexagon simulator through `hexagon-sim` and H2 `booter`.
- Benchmark corpus: 16 deterministic HQC-128 decode fixtures.
- Metric: simulator `Pcycles`; wall time is not used as a conclusion metric.
- Correctness gate: every recorded run reports `result=PASS`.

Full decode estimate:

```text
Pcycles/decode = (Pcycles_10iters - Pcycles_1iter) / (9 * 16)
```

Substage estimate:

```text
Pcycles/op = (Pcycles_10iters - Pcycles_1iter) / (ops_10iters - ops_1iter)
RM Pcycles/decode = Pcycles/block * 46
RS Pcycles/decode = Pcycles/op
```

## Variant Definitions

| Variant | Path | Flags |
| --- | --- | --- |
| `scalar` | `hqc_lab_scalar` | scalar Hexagon build, no HVX |
| `intrinsic_default_ct` | `hqc_lab_insintric` | default CT-oriented simulator flags: `HQC_GF_LUT_MUL=0`, `HQC_RS_FAST_NON_CT=0`, `HQC_RS_ROOTS_HVX=1`, `HQC_RM_FUSED_FAST=1` |
| `intrinsic_fastest_non_ct` | `hqc_lab_insintric` | `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, `HQC_RM_FUSED_FAST=1`, `HQC_RS_ROOTS_HVX=1` |

## Full Decode Raw Results

| Variant | Iters | Fixtures | Result | Total insns | Total Pcycles |
| --- | ---: | ---: | --- | ---: | ---: |
| `scalar` | 1 | 16 | PASS | 15,658,494 | 17,831,082 |
| `scalar` | 10 | 16 | PASS | 139,903,064 | 155,173,506 |
| `intrinsic_default_ct` | 1 | 16 | PASS | 5,268,713 | 6,724,140 |
| `intrinsic_default_ct` | 10 | 16 | PASS | 35,464,291 | 43,082,676 |
| `intrinsic_fastest_non_ct` | 1 | 16 | PASS | 3,370,774 | 4,427,013 |
| `intrinsic_fastest_non_ct` | 10 | 16 | PASS | 9,888,738 | 12,866,145 |

## Full Decode Derived Results

| Variant | Pcycles/decode | Speedup vs scalar | Pcycles reduction vs scalar |
| --- | ---: | ---: | ---: |
| `scalar` | 953,767 | 1.00x | 0.0% |
| `intrinsic_default_ct` | 252,490 | 3.78x | 73.5% |
| `intrinsic_fastest_non_ct` | 58,605 | 16.27x | 93.9% |

## Substage Results

Percent share is normalized within each variant's isolated substage sum. It is intended for pie or stacked-bar charts. The isolated substage sum is not forced to equal full decode Pcycles because setup, cache state, helper boundaries, and fused paths differ from the full benchmark.

| Variant | Substage | 1-iter Pcycles | 10-iter Pcycles | Delta ops | Pcycles/op | Pcycles/decode | Normalized share |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `scalar` | `rm_expand` | 36,735,447 | 58,298,250 | 6,624 | 3,255.3 | 149,742 | 14.6% |
| `scalar` | `rm_hadamard` | 38,551,581 | 76,447,593 | 6,624 | 5,721.0 | 263,167 | 25.6% |
| `scalar` | `rm_peak` | 35,478,852 | 45,734,010 | 6,624 | 1,548.2 | 71,216 | 6.9% |
| `scalar` | `rs_syndrome` | 36,937,221 | 60,309,711 | 144 | 162,309.0 | 162,309 | 15.8% |
| `scalar` | `rs_elp` | 36,253,779 | 53,474,832 | 144 | 119,590.6 | 119,591 | 11.6% |
| `scalar` | `rs_roots` | 36,032,442 | 51,262,656 | 144 | 105,765.4 | 105,765 | 10.3% |
| `scalar` | `rs_z` | 34,549,344 | 36,427,656 | 144 | 13,043.8 | 13,044 | 1.3% |
| `scalar` | `rs_error_values` | 36,630,621 | 57,237,888 | 144 | 143,106.0 | 143,106 | 13.9% |
| `scalar` | `rs_correct` | 34,347,660 | 34,410,825 | 144 | 438.6 | 439 | 0.0% |
| `intrinsic_default_ct` | `rm_expand` | 11,999,961 | 19,433,772 | 6,624 | 1,122.3 | 51,624 | 19.0% |
| `intrinsic_default_ct` | `rm_hadamard` | 11,477,823 | 14,200,395 | 6,624 | 411.0 | 18,907 | 7.0% |
| `intrinsic_default_ct` | `rm_peak` | 11,302,194 | 12,455,976 | 6,624 | 174.2 | 8,012 | 3.0% |
| `intrinsic_default_ct` | `rs_syndrome` | 11,312,871 | 12,556,593 | 144 | 8,637.0 | 8,637 | 3.2% |
| `intrinsic_default_ct` | `rs_elp` | 12,484,485 | 24,272,274 | 144 | 81,859.6 | 81,860 | 30.2% |
| `intrinsic_default_ct` | `rs_roots` | 11,229,960 | 11,726,382 | 144 | 3,447.4 | 3,447 | 1.3% |
| `intrinsic_default_ct` | `rs_z` | 11,353,362 | 12,958,422 | 144 | 11,146.2 | 11,146 | 4.1% |
| `intrinsic_default_ct` | `rs_error_values` | 12,573,663 | 25,158,894 | 144 | 87,397.4 | 87,397 | 32.2% |
| `intrinsic_default_ct` | `rs_correct` | 11,182,062 | 11,245,227 | 144 | 438.6 | 439 | 0.2% |
| `intrinsic_fastest_non_ct` | `rm_expand` | 5,942,901 | 8,965,128 | 6,624 | 456.3 | 20,988 | 29.1% |
| `intrinsic_fastest_non_ct` | `rm_hadamard` | 5,910,939 | 8,633,715 | 6,624 | 411.0 | 18,908 | 26.2% |
| `intrinsic_fastest_non_ct` | `rm_peak` | 5,704,398 | 6,579,972 | 6,624 | 132.2 | 6,080 | 8.4% |
| `intrinsic_fastest_non_ct` | `rs_syndrome` | 5,745,987 | 6,989,709 | 144 | 8,637.0 | 8,637 | 12.0% |
| `intrinsic_fastest_non_ct` | `rs_elp` | 5,733,915 | 6,868,530 | 144 | 7,879.3 | 7,879 | 10.9% |
| `intrinsic_fastest_non_ct` | `rs_roots` | 5,642,028 | 5,949,018 | 144 | 2,131.9 | 2,132 | 3.0% |
| `intrinsic_fastest_non_ct` | `rs_z` | 5,626,626 | 5,790,978 | 144 | 1,141.3 | 1,141 | 1.6% |
| `intrinsic_fastest_non_ct` | `rs_error_values` | 5,704,710 | 6,569,280 | 144 | 6,004.0 | 6,004 | 8.3% |
| `intrinsic_fastest_non_ct` | `rs_correct` | 5,615,178 | 5,678,343 | 144 | 438.6 | 439 | 0.6% |

## Substage Sum Check

| Variant | Isolated substage sum | Full decode Pcycles/decode | Substage sum / full decode |
| --- | ---: | ---: | ---: |
| `scalar` | 1,028,378 | 953,767 | 107.8% |
| `intrinsic_default_ct` | 271,469 | 252,490 | 107.5% |
| `intrinsic_fastest_non_ct` | 72,208 | 58,605 | 123.2% |

## Machine-Readable CSV

```csv
variant,kind,iters,stage,result,total_insns,total_pcycles,total_ops_or_decodes
scalar,full,1,full,PASS,15658494,17831082,16
scalar,full,10,full,PASS,139903064,155173506,160
intrinsic_default_ct,full,1,full,PASS,5268713,6724140,16
intrinsic_default_ct,full,10,full,PASS,35464291,43082676,160
intrinsic_fastest_non_ct,full,1,full,PASS,3370774,4427013,16
intrinsic_fastest_non_ct,full,10,full,PASS,9888738,12866145,160
scalar,substage,1,rm_expand,PASS,33178342,36735447,736
scalar,substage,10,rm_expand,PASS,52766479,58298250,7360
scalar,substage,1,rm_hadamard,PASS,34891750,38551581,736
scalar,substage,10,rm_hadamard,PASS,69893179,76447593,7360
scalar,substage,1,rm_peak,PASS,32420904,35478852,736
scalar,substage,10,rm_peak,PASS,45192874,45734010,7360
scalar,substage,1,rs_syndrome,PASS,33384145,36937221,16
scalar,substage,10,rs_syndrome,PASS,54820824,60309711,160
scalar,substage,1,rs_elp,PASS,32687434,36253779,16
scalar,substage,10,rs_elp,PASS,47852626,53474832,160
scalar,substage,1,rs_roots,PASS,32482824,36032442,16
scalar,substage,10,rs_roots,PASS,45808048,51262656,160
scalar,substage,1,rs_z,PASS,31194695,34549344,16
scalar,substage,10,rs_z,PASS,32924128,36427656,160
scalar,substage,1,rs_error_values,PASS,33066034,36630621,16
scalar,substage,10,rs_error_values,PASS,51635844,57237888,160
scalar,substage,1,rs_correct,PASS,31008809,34347660,16
scalar,substage,10,rs_correct,PASS,31065303,34410825,160
intrinsic_default_ct,substage,1,rm_expand,PASS,9874913,11999961,736
intrinsic_default_ct,substage,10,rm_expand,PASS,17857802,19433772,7360
intrinsic_default_ct,substage,1,rm_hadamard,PASS,9270657,11477823,736
intrinsic_default_ct,substage,10,rm_hadamard,PASS,11807862,14200395,7360
intrinsic_default_ct,substage,1,rm_peak,PASS,9049103,11302194,736
intrinsic_default_ct,substage,10,rm_peak,PASS,9599793,12455976,7360
intrinsic_default_ct,substage,1,rs_syndrome,PASS,9075484,11312871,16
intrinsic_default_ct,substage,10,rs_syndrome,PASS,9859827,12556593,160
intrinsic_default_ct,substage,1,rs_elp,PASS,10054357,12484485,16
intrinsic_default_ct,substage,10,rs_elp,PASS,19647469,24272274,160
intrinsic_default_ct,substage,1,rs_roots,PASS,9025951,11229960,16
intrinsic_default_ct,substage,10,rs_roots,PASS,9364247,11726382,160
intrinsic_default_ct,substage,1,rs_z,PASS,9101346,11353362,16
intrinsic_default_ct,substage,10,rs_z,PASS,10116327,12958422,160
intrinsic_default_ct,substage,1,rs_error_values,PASS,10089629,12573663,16
intrinsic_default_ct,substage,10,rs_error_values,PASS,19997483,25158894,160
intrinsic_default_ct,substage,1,rs_correct,PASS,8994852,11182062,16
intrinsic_default_ct,substage,10,rs_correct,PASS,9051346,11245227,160
intrinsic_fastest_non_ct,substage,1,rm_expand,PASS,4645549,5942901,736
intrinsic_fastest_non_ct,substage,10,rm_expand,PASS,7554454,8965128,7360
intrinsic_fastest_non_ct,substage,1,rm_hadamard,PASS,4605069,5910939,736
intrinsic_fastest_non_ct,substage,10,rm_hadamard,PASS,7142350,8633715,7360
intrinsic_fastest_non_ct,substage,1,rm_peak,PASS,4373947,5704398,736
intrinsic_fastest_non_ct,substage,10,rm_peak,PASS,4838525,6579972,7360
intrinsic_fastest_non_ct,substage,1,rs_syndrome,PASS,4409896,5745987,16
intrinsic_fastest_non_ct,substage,10,rs_syndrome,PASS,5194239,6989709,160
intrinsic_fastest_non_ct,substage,1,rs_elp,PASS,4409175,5733915,16
intrinsic_fastest_non_ct,substage,10,rs_elp,PASS,5185941,6868530,160
intrinsic_fastest_non_ct,substage,1,rs_roots,PASS,4346707,5642028,16
intrinsic_fastest_non_ct,substage,10,rs_roots,PASS,4562099,5949018,160
intrinsic_fastest_non_ct,substage,1,rs_z,PASS,4336889,5626626,16
intrinsic_fastest_non_ct,substage,10,rs_z,PASS,4461289,5790978,160
intrinsic_fastest_non_ct,substage,1,rs_error_values,PASS,4386577,5704710,16
intrinsic_fastest_non_ct,substage,10,rs_error_values,PASS,4956495,6569280,160
intrinsic_fastest_non_ct,substage,1,rs_correct,PASS,4329264,5615178,16
intrinsic_fastest_non_ct,substage,10,rs_correct,PASS,4385758,5678343,160
```

```csv
variant,substage,pcycles_per_op,pcycles_per_decode,normalized_pct
scalar,rm_expand,3255.3,149742,14.6
scalar,rm_hadamard,5721.0,263167,25.6
scalar,rm_peak,1548.2,71216,6.9
scalar,rs_syndrome,162309.0,162309,15.8
scalar,rs_elp,119590.6,119591,11.6
scalar,rs_roots,105765.4,105765,10.3
scalar,rs_z,13043.8,13044,1.3
scalar,rs_error_values,143106.0,143106,13.9
scalar,rs_correct,438.6,439,0.0
intrinsic_default_ct,rm_expand,1122.3,51624,19.0
intrinsic_default_ct,rm_hadamard,411.0,18907,7.0
intrinsic_default_ct,rm_peak,174.2,8012,3.0
intrinsic_default_ct,rs_syndrome,8637.0,8637,3.2
intrinsic_default_ct,rs_elp,81859.6,81860,30.2
intrinsic_default_ct,rs_roots,3447.4,3447,1.3
intrinsic_default_ct,rs_z,11146.2,11146,4.1
intrinsic_default_ct,rs_error_values,87397.4,87397,32.2
intrinsic_default_ct,rs_correct,438.6,439,0.2
intrinsic_fastest_non_ct,rm_expand,456.3,20988,29.1
intrinsic_fastest_non_ct,rm_hadamard,411.0,18908,26.2
intrinsic_fastest_non_ct,rm_peak,132.2,6080,8.4
intrinsic_fastest_non_ct,rs_syndrome,8637.0,8637,12.0
intrinsic_fastest_non_ct,rs_elp,7879.3,7879,10.9
intrinsic_fastest_non_ct,rs_roots,2131.9,2132,3.0
intrinsic_fastest_non_ct,rs_z,1141.3,1141,1.6
intrinsic_fastest_non_ct,rs_error_values,6004.0,6004,8.3
intrinsic_fastest_non_ct,rs_correct,438.6,439,0.6
```

## Notes

- `intrinsic_fastest_non_ct` is benchmark-only. It uses side-channel-relaxed RS control flow and data-dependent GF/RM lookup tables.
- Current `hqc_lab_insintric` scripts run the fastest path by default now; use `HQC128_BENCH_ITERS=10 ./hqc_lab_insintric/scripts/run_hqc128_decode_bench_hexagon.sh` instead of passing the old fastest env flags.
- Scalar substage support is benchmark-only and was added to measure the same RM/RS stages as the intrinsic benchmark.
- The intrinsic Reed-Muller code was refactored before these measurements. Post-refactor full-decode checks showed no meaningful performance loss: default CT measured 252,490 Pcycles/decode and fastest non-CT measured 58,605 Pcycles/decode.
