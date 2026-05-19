# HQC-128 Hexagon Lab Notes

## Current layout

- `hqc_lab_scalar/`: portable scalar C baseline. Its Hexagon decode benchmark intentionally builds without `-mhvx`.
- `hqc_lab_insintric/`: Hexagon HVX intrinsic variant. The folder name keeps the spelling requested in the task.
- Both labs use the same generated decode fixture corpus in `fixtures/hqc128_decode_fixture.c`: 16 deterministic random HQC-128 codewords with 0..15 corrupted RS-symbol blocks and the expected recovered 16-byte messages.

## Implementation summary

| Pass | Brief change | Main file / section |
| --- | --- | --- |
| 1 | Split scalar baseline from HVX intrinsic build; added HVX RM Hadamard add/sub and peak absolute-max reduction. | `hqc_lab_scalar/src/ref/reed_muller.c` baseline; `hqc_lab_insintric/src/ref/reed_muller.c`: `hadamard_hvx`, `find_peaks_hvx`, `reed_muller_decode` |
| 2 | Removed scalar even/odd gather from HVX Hadamard by using HVX deal/deinterleave. | `hqc_lab_insintric/src/ref/reed_muller.c`: `hadamard_hvx` |
| 3 | Replaced scalar peak scan with HVX predicate/index-vector tie-break logic. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_index_lo`, `rm_index_hi`, `find_peaks_hvx` |
| 4 | Added HVX RM expand/sum path and RM-vs-RS stage benchmark. | `hqc_lab_insintric/src/ref/reed_muller.c`: `expand_and_sum_hvx`; `hqc_lab_insintric/demos/hqc128_decode_stage_bench.c`; `hqc_lab_insintric/scripts/run_hqc128_decode_stage_bench_hexagon.sh` |
| 5 | Added fixed-flow hardware-style GF multiplier and opt-in GF LUT experiment. | `hqc_lab_insintric/src/ref/gf.c`: `gf_mul_hwstyle`, `gf_mul`; intrinsic benchmark scripts: `HQC_USE_GF_HWSTYLE_MUL`, `HQC_GF_LUT_MUL` |
| 6 | Vectorized RS syndrome computation with HVX and defaulted it in intrinsic Hexagon scripts. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_syndromes_hvx`; `hqc_lab_insintric/scripts/run_hqc128_decode_bench_hexagon.sh`: `HQC_HVX_RS_SYNDROME` |
| 7 | Targeted top substage bottlenecks: packed RM expand/sum, shortened RS Chien roots, and gated fast non-CT RS ELP/error-values experiments. | `hqc_lab_insintric/src/ref/reed_muller.c`: `expand_and_sum_hvx`; `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_elp`, `compute_roots`, `compute_error_values`; `hqc_lab_insintric/demos/hqc128_decode_substage_bench.c` |
| 8 | Optimized the fastest benchmark-only RS/GF path with a full GF(256) multiplication table and LUT inverse. | `hqc_lab_insintric/src/ref/gf.c`: `gf_mul_table`, `gf_mul`, `gf_inverse`; enabled by `HQC_GF_LUT_MUL=1` |
| 9 | Added benchmark-only RM expand/sum table lookup and degree-bound fast RS roots/error-values. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_expand3_nibble_table`, `expand_and_sum_hvx`; `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_roots`, `compute_error_values`; enabled by `HQC_RM_EXPAND_LUT=1` plus fast RS/GF flags |
| 10 | Added benchmark-only fused RM expand/Hadamard/peak path for full decode. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_decode_one_hvx_fast`, `reed_muller_decode`; `hqc_lab_insintric/scripts/run_hqc128_decode_bench_hexagon.sh`: `HQC_RM_FUSED_FAST` |
| 11 | Tightened the fast RS algebra path: degree-bound BM auxiliary update, degree-bound z polynomial, and derivative-based Forney denominator. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_elp`, `compute_z_poly`, `compute_error_values`; `hqc_lab_insintric/demos/hqc128_decode_substage_bench.c` |
| 12 | Added benchmark-only HVX Chien root evaluation across the 46 shortened RS support positions. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_roots_hvx`, `rs_support_powers`; Hexagon scripts: `HQC_RS_ROOTS_HVX` |
| 13 | Promoted CT-safe default optimizations: fixed-loop HVX roots, fixed-flow RS-local GF arithmetic, derivative error values, and CT RM peak sign recovery. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `rs_gf_mul_ct`, `compute_roots_hvx`, `compute_error_values`; `hqc_lab_insintric/src/ref/reed_muller.c`: `find_peaks_hvx`, `rm_decode_one_hvx_fast`; Hexagon scripts default `HQC_RS_ROOTS_HVX=1`, `HQC_RM_FUSED_FAST=1` |

## Implemented so far

The original `hqc_lab/` was split into scalar and intrinsic variants.

The first intrinsic pass targeted Reed-Muller decode because it repeatedly processes 128 signed halfword lanes per RS byte:

- `hadamard_hvx`: replaced scalar halfword add/sub in the RM(1,7) Hadamard transform with HVX `vadd.h` and `vsub.h`.
- `find_peaks_hvx`: replaced scalar absolute-value maximum reduction with HVX `vabs.h`, `vmax.h`, and `vror`.
- Reed-Solomon, GF arithmetic, and FFT remain scalar because they are smaller, table-heavy, and more dependency-heavy.

Disassembly confirmed that the intrinsic binary contains HVX instructions such as `vmem`, `vadd`, `vsub`, `vabs`, `vmax`, and `vror`.

The second intrinsic pass removed the scalar even/odd gather inside `hadamard_hvx`. A simulator probe verified this lane transform:

1. `Q6_W_vdeal_VVR(hi, lo, 2)` groups even and odd halfword lanes across the two 128-byte vectors.
2. `Q6_Vh_vdeal_Vh` on each output vector finishes the deinterleave.

The main binary disassembly now shows `vdeal`, `vadd`, `vsub`, `vabs`, `vmax`, and `vror` in the Reed-Muller decode path.

The third intrinsic pass removed the scalar scan from `find_peaks_hvx`. It now uses HVX equality predicates, `vmux`, and `vmin` reduction over explicit index vectors to preserve the reference tie-break rule: choose the smallest position with maximal absolute value.

The fourth intrinsic pass changed `expand_and_sum` for the intrinsic build. Bit extraction is still scalar, but the three RM repetitions are expanded into aligned temporary vectors and summed with HVX halfword adds. This was kept only after the simulator benchmark showed a further reduction.

A stage benchmark was added in `hqc_lab_insintric/demos/hqc128_decode_stage_bench.c` with `hqc_lab_insintric/scripts/run_hqc128_decode_stage_bench_hexagon.sh`. It separates Reed-Muller and Reed-Solomon decode cost so the next optimization target is based on counters rather than guesswork.

The fifth pass used `git/pqc-hqc-hardware/hardware/decap/gfmul.v` as the reference shape for GF multiplication. The new default intrinsic build uses an unrolled `xtime`/xor GF multiplier behind `HQC_USE_GF_HWSTYLE_MUL`. This keeps GF multiplication arithmetic and fixed-flow, unlike the faster LUT experiment. A first looped version was correct but slower; only the unrolled version was promoted after stage and full-decode benchmarks improved.

The sixth pass vectorized RS syndrome computation with HVX. For each received RS byte, it multiplies that byte by the 30 syndrome constants in parallel across halfword lanes, using the same fixed-flow GF multiply shape as the fifth pass. This is now enabled by default in the Hexagon intrinsic scripts through `HQC_HVX_RS_SYNDROME=1`; set `HQC_HVX_RS_SYNDROME=0` to compare against the fifth pass.

The fixture generator now emits a 16-case deterministic random corpus instead of one fixed case. Each benchmark iteration decodes all 16 fixtures, covering random messages and RS-symbol error counts from 0 through `PARAM_DELTA`.

An intrinsic-only substage benchmark was added in `hqc_lab_insintric/demos/hqc128_decode_substage_bench.c` with `hqc_lab_insintric/scripts/run_hqc128_decode_substage_bench_hexagon.sh`. It is compiled with `HQC_ENABLE_SUBSTAGE_BENCH=1` and does not change the production decode path. Timed loops use a small checksum instead of full `memcmp` validation to avoid measuring the verifier more than the target substage.

An opt-in GF multiplication LUT experiment was added behind `HQC_GF_LUT_MUL=1`. This is not the default intrinsic path because it changes GF multiplication from arithmetic constant-time code to data-dependent table lookups. It is useful as a benchmark/engineering tradeoff point, not automatically suitable for production crypto.

The seventh pass targeted the four measured bottlenecks from the substage table:

- `expand_and_sum_hvx` now expands four input bits at a time into packed 16-bit lanes and sums the three RM repetitions with 64-bit arithmetic. This removes the aligned temporary copy vectors and most scalar per-bit stores.
- RS ELP keeps the masked fixed-flow Berlekamp-Massey implementation by default. `HQC_RS_FAST_NON_CT=1` switches to a standard branchy Berlekamp-Massey implementation.
- RS roots now use a shortened-code Chien search over the public `PARAM_N1 = 46` RS positions instead of evaluating the locator over all 256 GF elements with additive FFT. The root test uses a constant-time zero mask.
- The original additive-FFT RS root finder is kept as an opt-in reference path. Set `HQC_RS_ROOTS_FFT=1` in the intrinsic host, full-decode, stage, or substage benchmark scripts to compare against the default Chien search.
- RS error values keep the fixed-loop computation over `PARAM_DELTA = 15` and the 46 shortened RS positions by default. `HQC_RS_FAST_NON_CT=1` switches to an actual-located-error loop.

This seventh pass was the first default constant-time-oriented path. Its default GF multiplication remains arithmetic and fixed-flow. `HQC_GF_LUT_MUL=1` is still available as an opt-in speed experiment, but it uses data-dependent table lookups and should not be treated as constant-time production crypto without a separate threat-model decision.

The seventh pass now has two RS modes:

- Default resistance mode: no extra flag. This keeps fixed-flow ELP and error-value computation.
- Fast non-resistance mode: set `HQC_RS_FAST_NON_CT=1` in the host, Hexagon full-decode, or Hexagon substage benchmark scripts. This mode is for performance comparison only because ELP and error-value work depend on the decoded error structure.

The eighth pass only changes the opt-in GF LUT benchmark path. With `HQC_GF_LUT_MUL=1`, `gf_mul` now initializes and uses a 64 KiB GF(256) multiplication table, and `gf_inverse` uses the `gf_log`/`gf_exp` tables directly. This is intentionally not promoted to the default constant-time-oriented build because it adds data-dependent table access and one-time mutable table initialization, but it is the fastest measured decode benchmark path when side-channel behavior is out of scope.

The ninth pass keeps the default path unchanged and targets the side-channel-relaxed fastest benchmark mode. With `HQC_RM_EXPAND_LUT=1`, RM expand/sum uses a 32 KiB table that maps the three repeated RM codeword nibbles directly to four packed 16-bit sums. In `HQC_RS_FAST_NON_CT=1`, RS roots and error-values now use the actual Berlekamp-Massey degree instead of looping to `PARAM_DELTA` when evaluating the locator and numerator polynomials.

The tenth pass keeps the substage benchmark path unchanged and adds a full-decode-only fused RM path behind `HQC_RM_FUSED_FAST=1`. It expands one RM block, runs the HVX Hadamard transform, and performs the HVX peak reduction inside one inlined function. This removes one function boundary and the intermediate handoff between the generic expand and Hadamard helpers in the fastest side-channel-relaxed benchmark mode.

The eleventh pass targets the remaining RS algebra in the side-channel-relaxed fastest mode. The current software path keeps the branchy Berlekamp-Massey shape instead of switching to the heavier inversionless BM architecture from Wu 2015, because the GF table path already makes inversions cheap and the inversionless update would multiply whole polynomials every iteration. The adopted changes are narrower: track the actual degree of the BM auxiliary polynomial `b`, compute `z(x)` only to the actual locator degree, and use the formal derivative of `sigma(x)` as the Forney denominator for error values. This reuses the Wu/Forney observation that the derivative/odd locator terms can replace the explicit product over all other error locators.

The twelfth pass adds an opt-in HVX Chien search for the fastest mode. It precomputes the powers of the 46 shortened RS support points into aligned 64-lane vectors and evaluates `sigma(x)` as a vector sum of `sigma[j] * x^j`. This deliberately does not use the GF table path inside the vector operation; the measured win comes from evaluating all support positions in one HVX vector, while keeping the scalar Chien path as the default and as the fallback when `HQC_RS_ROOTS_HVX=0`.

The thirteenth pass keeps the default side-channel-resistant strategy but moves the safe parts of the faster experiments into the default Hexagon build. The HVX Chien backend now has a default fixed-flow branch that always evaluates all `PARAM_DELTA + 1` locator coefficients, avoids the `sigma[j] != 0` branch, and writes only the public 46 shortened-support positions. Reed-Solomon now uses RS-local inlined fixed-flow GF multiply/square/inverse helpers when `HQC_USE_GF_HWSTYLE_MUL=1` and `HQC_GF_LUT_MUL=0`, removing call overhead without introducing secret-indexed tables. Default RS error values now use the formal derivative denominator in fixed `PARAM_DELTA` loops instead of multiplying over the other located errors. RM peak handling no longer loads `transform[peak_pos]` in the default path; it recovers the peak sign with vector compare/mux/reduction. The side-channel-relaxed fastest path keeps its old fused RM indexed load under `HQC_RS_FAST_NON_CT=1`, so the fastest benchmark result is unchanged.

Current run note: `hqc_lab_insintric/scripts/run_hqc128_decode_bench_hexagon.sh` now runs the fastest path by default, so the old fastest env flags are no longer needed. Use `HQC128_BENCH_ITERS=10 ./hqc_lab_insintric/scripts/run_hqc128_decode_bench_hexagon.sh` for the standard simulator benchmark.

## Benchmark results

When a candidate is correctness-safe and improves simulator Pcycles, this file should be updated with the new result before moving on.

All runs used `demos/hqc128_decode_bench.c`, the same fixture, and `HQC128_BENCH_ITERS=100` unless noted.

| Variant | Result | Total insns | Total Pcycles | Wall time |
| --- | --- | ---: | ---: | ---: |
| `hqc_lab_scalar` | PASS | 87,945,846 | 97,724,244 | 24.55s |
| `hqc_lab_insintric` first pass | PASS | 82,938,131 | 91,779,186 | 24.52s |
| `hqc_lab_insintric` second pass | PASS | 70,347,931 | 79,345,386 | 21.15s |
| `hqc_lab_insintric` third pass | PASS | 62,728,180 | 72,489,072 | 19.87s |
| `hqc_lab_insintric` fourth pass | PASS | 60,567,603 | 69,123,735 | 16.82s |
| `hqc_lab_insintric` fifth pass, hardware-style GF | PASS | 44,280,957 | 64,426,629 | not timed |
| `hqc_lab_insintric` sixth pass, HVX RS syndrome | PASS | 35,356,810 | 50,178,084 | not timed |
| `hqc_lab_insintric` fourth pass + opt-in GF LUT | PASS | 27,502,984 | 36,925,356 | 9.30s |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | PASS | 24,248,761 | 31,991,607 | not timed |

One-iteration runs were used to estimate fixed simulator/boot overhead:

| Variant | Result | Total insns | Total Pcycles | Wall time |
| --- | --- | ---: | ---: | ---: |
| `hqc_lab_scalar`, 1 iter | PASS | 2,683,701 | 3,453,807 | 2.39s |
| `hqc_lab_insintric`, 1 iter | PASS | 2,634,302 | 3,395,586 | 4.44s |
| `hqc_lab_insintric` second pass, 1 iter | PASS | 2,508,400 | 3,271,248 | 1.93s |
| `hqc_lab_insintric` third pass, 1 iter | PASS | 2,433,835 | 3,204,651 | not timed |
| `hqc_lab_insintric` fourth pass, 1 iter | PASS | 2,413,044 | 3,171,951 | not timed |
| `hqc_lab_insintric` fifth pass, hardware-style GF, 1 iter | PASS | 2,250,132 | 3,124,677 | not timed |
| `hqc_lab_insintric` sixth pass, HVX RS syndrome, 1 iter | PASS | 2,173,813 | 3,001,452 | not timed |
| `hqc_lab_insintric` fourth pass + opt-in GF LUT, 1 iter | PASS | 2,083,567 | 2,851,770 | not timed |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT, 1 iter | PASS | 2,063,872 | 2,821,491 | not timed |

Approximate per-decode cost from `(100-iter Pcycles - 1-iter Pcycles) / 99`:

| Variant | Estimated Pcycles/decode |
| --- | ---: |
| Scalar | 952,226 |
| First intrinsic pass | 892,764 |
| Second intrinsic pass | 768,426 |
| Third intrinsic pass | 699,843 |
| Fourth intrinsic pass | 666,180 |
| Fifth pass, hardware-style GF | 619,212 |
| Sixth pass, HVX RS syndrome | 476,532 |
| Fourth intrinsic pass + opt-in GF LUT | 344,178 |
| Sixth pass + opt-in GF LUT | 294,648 |

The first intrinsic pass was about 6.2% faster than scalar by estimated Pcycles/decode. The fourth pass was about 30.0% faster than scalar. The fifth pass was about 35.0% faster than scalar. In this older single-fixture table, the sixth pass is about 50.0% faster than scalar and about 23.0% faster than the fifth pass by estimated Pcycles/decode. The sixth-pass opt-in GF LUT combo is about 69.1% faster than scalar and about 38.2% faster than the sixth pass, but carries the side-channel caveat above. The current seventh-pass result is measured in the 16-fixture corpus table below. Wall time is less reliable than Pcycles because `hexagon-sim` startup and simulation overhead are significant.

Stage benchmark snapshots:

| Variant | Stage | Iters | Result | Total Pcycles |
| --- | --- | ---: | --- | ---: |
| Fourth pass default | RM | 100 | PASS | 15,658,983 |
| Fourth pass default | RS | 100 | PASS | 57,289,110 |
| Fifth pass, hardware-style GF | RS | 100 | PASS | 52,545,096 |
| Sixth pass, HVX RS syndrome | RS | 100 | PASS | 38,153,673 |
| Fourth pass + opt-in GF LUT | RS | 100 | PASS | 24,768,837 |
| Sixth pass + opt-in GF LUT | RS | 100 | PASS | 19,785,558 |

Corpus benchmark snapshots:

These runs used the 16-fixture corpus. `HQC128_BENCH_ITERS=10` means 160 total decodes; `HQC128_BENCH_ITERS=1` means 16 total decodes. Estimated Pcycles/decode uses `(10-iter Pcycles - 1-iter Pcycles) / (9 * 16)`.

| Variant | Iters | Fixtures | Result | Total insns | Total Pcycles |
| --- | ---: | ---: | --- | ---: | ---: |
| `hqc_lab_scalar` | 10 | 16 | PASS | 139,903,064 | 155,173,506 |
| `hqc_lab_scalar` | 1 | 16 | PASS | 15,658,494 | 17,831,082 |
| `hqc_lab_insintric` sixth pass | 10 | 16 | PASS | 55,750,933 | 79,085,736 |
| `hqc_lab_insintric` sixth pass | 1 | 16 | PASS | 7,257,851 | 10,243,392 |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | 10 | 16 | PASS | 37,977,434 | 49,986,384 |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | 1 | 16 | PASS | 5,481,600 | 7,335,336 |
| `hqc_lab_insintric` seventh pass CT | 10 | 16 | PASS | 46,334,532 | 66,027,102 |
| `hqc_lab_insintric` seventh pass CT | 1 | 16 | PASS | 6,311,098 | 8,930,406 |
| `hqc_lab_insintric` seventh pass CT + opt-in GF LUT | 10 | 16 | PASS | 28,340,550 | 36,530,676 |
| `hqc_lab_insintric` seventh pass CT + opt-in GF LUT | 1 | 16 | PASS | 4,512,796 | 5,982,540 |
| `hqc_lab_insintric` seventh pass fast non-CT | 10 | 16 | PASS | 32,359,087 | 45,166,485 |
| `hqc_lab_insintric` seventh pass fast non-CT | 1 | 16 | PASS | 4,912,694 | 6,843,132 |
| `hqc_lab_insintric` seventh pass fast non-CT + opt-in GF LUT | 10 | 16 | PASS | 20,979,590 | 26,504,094 |
| `hqc_lab_insintric` seventh pass fast non-CT + opt-in GF LUT | 1 | 16 | PASS | 3,775,854 | 4,978,710 |
| `hqc_lab_insintric` eighth pass fast non-CT + GF table LUT | 10 | 16 | PASS | 17,790,289 | 21,116,241 |
| `hqc_lab_insintric` eighth pass fast non-CT + GF table LUT | 1 | 16 | PASS | 4,034,876 | 5,080,200 |
| `hqc_lab_insintric` ninth pass fast non-CT + GF table LUT + RM LUT | 10 | 16 | PASS | 11,192,310 | 14,187,261 |
| `hqc_lab_insintric` ninth pass fast non-CT + GF table LUT + RM LUT | 1 | 16 | PASS | 3,485,005 | 4,534,398 |
| `hqc_lab_insintric` tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 10 | 16 | PASS | 11,086,171 | 14,111,097 |
| `hqc_lab_insintric` tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 1 | 16 | PASS | 3,464,978 | 4,517,850 |
| `hqc_lab_insintric` eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 10 | 16 | PASS | 10,398,737 | 13,185,531 |
| `hqc_lab_insintric` eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 1 | 16 | PASS | 3,396,303 | 4,425,477 |
| `hqc_lab_insintric` twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 10 | 16 | PASS | 9,812,288 | 12,855,702 |
| `hqc_lab_insintric` twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 1 | 16 | PASS | 3,353,940 | 4,417,002 |
| `hqc_lab_insintric` thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 10 | 16 | PASS | 35,648,131 | 43,766,676 |
| `hqc_lab_insintric` thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 1 | 16 | PASS | 5,287,097 | 6,792,540 |
| `hqc_lab_insintric` thirteenth pass fastest non-CT regression check | 10 | 16 | PASS | 9,812,288 | 12,855,702 |
| `hqc_lab_insintric` thirteenth pass fastest non-CT regression check | 1 | 16 | PASS | 3,353,940 | 4,417,002 |

| Variant | Corpus-estimated Pcycles/decode |
| --- | ---: |
| Scalar | 953,767 |
| Sixth pass, HVX RS syndrome | 478,072 |
| Sixth pass + opt-in GF LUT | 296,188 |
| Seventh pass CT | 396,505 |
| Seventh pass CT + opt-in GF LUT | 212,140 |
| Seventh pass fast non-CT | 266,134 |
| Seventh pass fast non-CT + opt-in GF LUT | 149,482 |
| Eighth pass fast non-CT + GF table LUT | 111,361 |
| Ninth pass fast non-CT + GF table LUT + RM LUT | 67,034 |
| Tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 66,620 |
| Eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 60,834 |
| Twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 58,602 |
| Thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 256,765 |


On the corpus benchmark, the seventh-pass default CT path is about 58.4% faster than scalar and about 17.1% faster than the sixth pass. The thirteenth-pass default CT path is about 73.1% faster than scalar, about 35.2% faster than the seventh-pass CT path, and about 46.3% faster than the sixth pass. The seventh-pass fast non-CT path is about 72.1% faster than scalar and about 44.3% faster than the sixth pass, but it leaks decoded-error structure through branchy RS control flow. The twelfth-pass fastest measured combination remains 58,602 Pcycles/decode after the thirteenth-pass changes. It combines branchy RS leakage with data-dependent GF/RM table lookups, HVX root evaluation, and the fused RM benchmark path, so it remains benchmark-only.

Default intrinsic substage snapshots:

These runs used the 16-fixture corpus. `HQC128_BENCH_ITERS=10` and `HQC128_BENCH_ITERS=1` were both run for every substage. Estimated cost is `(10-iter Pcycles - 1-iter Pcycles) / (9 * operation_count_per_iteration)`. RM substages report both per RM block and per full decode, where one decode has `PARAM_N1 = 46` RM blocks.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost |
| --- | ---: | ---: | ---: |
| RM expand/sum | 36,534,192 | 20,038,749 | 2,490/block; 114,552/decode |
| RM Hadamard | 21,232,335 | 18,509,763 | 411/block; 18,907/decode |
| RM find peak | 19,112,301 | 18,296,346 | 123/block; 5,666/decode |
| RS syndrome | 18,802,773 | 18,266,238 | 3,726/decode |
| RS ELP | 35,757,174 | 19,961,721 | 109,691/decode |
| RS roots/FFT | 33,920,784 | 19,777,914 | 98,214/decode |
| RS Z polynomial | 20,092,398 | 18,395,526 | 11,784/decode |
| RS error values | 39,174,546 | 20,303,742 | 131,047/decode |
| RS correction | 18,277,167 | 18,213,798 | 440/decode |

At that snapshot, the dominant default-intrinsic costs were RS error values, RM expand/sum, RS ELP, and RS roots/FFT. RM Hadamard, RM peak search, RS syndrome, and final correction were no longer primary targets.

Seventh-pass CT substage snapshots with the default non-LUT fixed-flow GF path:

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 24,532,173 | 16,482,330 | 1,215/block; 55,902/decode | 51.2% lower |
| RS ELP | 33,139,155 | 17,343,702 | 109,691/decode | unchanged |
| RS roots/Chien | 27,627,885 | 16,792,407 | 75,246/decode | 23.4% lower |
| RS error values | 36,556,527 | 17,685,927 | 131,046/decode | unchanged |

Thirteenth-pass CT default substage snapshots with fixed-flow RS-local GF helpers, fixed-loop HVX roots, derivative error values, and no default secret-indexed RM peak load:

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs seventh-pass CT |
| --- | ---: | ---: | ---: | ---: |
| RS ELP | 24,466,578 | 12,678,789 | 81,860/decode | 25.4% lower |
| RS roots/HVX Chien | 11,920,686 | 11,424,264 | 3,447/decode | 95.4% lower |
| RS error values | 25,640,238 | 12,796,875 | 89,190/decode | 31.9% lower |

Two full-decode isolation runs were used to check which default CT pieces mattered. With the thirteenth-pass RS arithmetic and error-value changes but both `HQC_RS_ROOTS_HVX=0` and `HQC_RM_FUSED_FAST=0`, full decode was 316,222 Pcycles/decode. With `HQC_RS_ROOTS_HVX=1` and `HQC_RM_FUSED_FAST=0`, it was 258,844 Pcycles/decode. With both thirteenth-pass defaults enabled, it was 256,765 Pcycles/decode. Most of the full-decode win therefore comes from fixed-flow HVX roots and RS arithmetic/error-value changes; arithmetic fused RM is a small default win after the CT peak-sign fix.

Seventh-pass fast non-CT substage snapshots with opt-in GF LUT and non-fixed-flow RS/GF behavior:

These runs used `HQC_RS_FAST_NON_CT=1` and `HQC_GF_LUT_MUL=1`. They are useful for performance comparison, but not for a side-channel-resistant default.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 16,544,778 | 8,494,731 | 1,215/block; 55,903/decode | 51.2% lower |
| RS ELP | 11,498,550 | 7,990,782 | 24,360/decode | 77.8% lower |
| RS roots/Chien | 12,022,890 | 8,043,048 | 27,638/decode | 71.9% lower |
| RS error values | 11,055,858 | 7,947,021 | 21,589/decode | 83.5% lower |

Ninth-pass fast non-CT substage snapshots with GF table LUT, RM expand LUT, and degree-bound RS:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, and `HQC_RM_EXPAND_LUT=1`.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs eighth/seventh fast bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 9,192,684 | 6,170,457 | 456/block; 20,988/decode | 62.4% lower vs eighth-pass fast |
| RS roots/Chien | 7,318,650 | 5,983,608 | 9,271/decode | 66.5% lower vs seventh-pass fast |
| RS error values | 6,990,762 | 5,951,475 | 7,217/decode | 66.6% lower vs seventh-pass fast |

Eleventh-pass fast non-CT RS algebra substage snapshots:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, and `HQC_RM_EXPAND_LUT=1`. The full fastest decode also used `HQC_RM_FUSED_FAST=1`, but the RS substage benchmark does not exercise that RM-only flag.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior fast substage |
| --- | ---: | ---: | ---: | ---: |
| RS ELP/Berlekamp-Massey | 6,989,178 | 5,854,563 | 7,879/decode | 29.8% lower vs degree-unbounded fast BM update |
| RS z polynomial | 5,853,102 | 5,688,750 | 1,141/decode | 61.5% lower vs fixed-loop fast z |
| RS error values | 6,592,524 | 5,727,954 | 6,004/decode | 16.8% lower vs explicit denominator product |

Twelfth-pass fast non-CT HVX roots substage snapshot:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, and `HQC_RS_ROOTS_HVX=1`.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior fast substage |
| --- | ---: | ---: | ---: | ---: |
| RS roots/HVX Chien | 5,930,871 | 5,623,881 | 2,132/decode | 77.0% lower vs scalar degree-bound Chien |

## Next optimization candidates

1. Done: remove scalar even/odd gather from `hadamard_hvx`.
   The first pass copied 128 halfwords into temporary `even[]` and `odd[]` arrays each Hadamard pass. The second pass replaced this with verified HVX deal/deinterleave instructions.

2. Partially done: restructure `expand_and_sum`.
   The fourth pass still extracts bits with scalar shifts and masks, but it moves the summation across RM repetitions to HVX. A more aggressive version would need either a verified LUT path or a data-layout change.

3. Done: make `find_peaks_hvx` return the index with vector logic.
   The third pass uses vector index selection and min reduction to preserve tie-breaking semantics.

4. Add stage-level timing or counters.
   Added at RM-vs-RS granularity. A deeper version can split RM expansion, Hadamard, peak search, syndrome computation, ELP, FFT roots, and error values.

5. Done: port the hardware GF multiplier shape to C and unroll it.
   The looped form was correct but slower. The unrolled form is now the default intrinsic GF path because it improved RS stage Pcycles from 57,289,110 to 52,545,096 and full estimated decode cost from 666,180 to 619,212 Pcycles/decode without adopting data-dependent table lookup.

6. Done: vectorize RS syndrome computation with HVX.
   The syndrome step matches HVX well because it computes 30 independent GF products per RS byte. The sixth pass improved RS stage Pcycles from 52,545,096 to 38,153,673 and full estimated decode cost from 619,212 to 476,532 Pcycles/decode.

7. Done: target the current top four bottlenecks.
   The constant-time-oriented seventh pass keeps the safe wins: RM packed expansion and shortened Chien root search over public RS positions. The branchy BM and actual-error value experiments are still available behind `HQC_RS_FAST_NON_CT=1`, but the default path leaves them off because they make RS control flow depend on decoded error structure.

8. Done: promote CT-safe versions of the strongest measured default-path ideas.
   The thirteenth pass makes HVX Chien roots default only after removing the degree-dependent loop and coefficient-dependent branch from the default path. It also keeps GF arithmetic fixed-flow while inlining it into RS, uses the derivative denominator for error values with fixed `PARAM_DELTA` loops, and removes the default secret-indexed RM peak-value load. The fastest side-channel-relaxed path is still available and unchanged in measured Pcycles/decode.

## Confidence notes

The safe second-pass step was candidate 1: replacing the scalar even/odd gather with HVX deal/deinterleave. It was narrower than rewriting `expand_and_sum`, kept the same data representation, and directly fixed a measured weakness in the first intrinsic pass.

The stage benchmark shows RS/GF is now the dominant default cost after RM intrinsic work. The remaining high-risk candidates are:

- A fuller `expand_and_sum` rewrite using `vlut16`, gather/scatter, or a data-layout change. It may no longer be the best next target because RM is no longer dominant.
- A vectorized RS/GF path. This has higher upside, but proving correctness across all GF inputs and avoiding data-dependent lookup side channels are the main loopholes.
- A deeper RS/RM stage benchmark. Done at substage granularity for the current intrinsic default. After the thirteenth pass, RS roots are no longer a default bottleneck; the remaining default targets are RS error values, RS ELP, RM expand/sum, and z polynomial, not RS syndrome or RM Hadamard.

The hardware-style GF path is the safest measured scalar improvement from the paper/Verilog review: it is correct for all 256x256 GF input pairs in a host self-test, passes host and simulator decode, and is fixed-flow arithmetic. The HVX syndrome path builds on that same arithmetic shape and is enabled because it maps independent syndrome lanes directly to HVX lanes. The opt-in GF LUT path is still factually faster on the simulator, but it should stay opt-in until the threat model accepts data-dependent table lookup on RS intermediate values.

For the seventh pass, confidence is scoped to correctness on the 16-fixture corpus, measured Hexagon simulator Pcycles, and the implemented constant-time strategy. The loopholes found during strategy review were side-channel behavior, uncorrectable-codeword behavior, shortened-RS root mapping, accidental promotion of the GF LUT path, and accidental promotion of branchy RS code. The fixes applied were: keep masked fixed-flow ELP and error-value computation in the default build, keep correction tests across 0..15 RS-symbol errors, make the root search cover exactly the public shortened RS positions `0..PARAM_N1-1`, use a constant-time root-zero test, leave `HQC_GF_LUT_MUL=1` as an explicitly opt-in benchmark path, and gate branchy RS code behind `HQC_RS_FAST_NON_CT=1`.

For the eighth pass, confidence is scoped to the benchmark-only side-channel-relaxed mode requested for speed comparison. The loopholes checked were incorrect GF table generation, inverse behavior for zero, one-time table initialization overhead, accidental changes to the default CT path, and regression against the previous fastest measured result. The fixes/checks were: keep the table path behind `HQC_GF_LUT_MUL=1`, keep `gf_inverse(0) = 0`, verify `gf_mul` against the 256x256 LUT reference self-test, run host decode in both default and fast LUT modes, and rerun Hexagon simulator 1-iter and 10-iter corpus benchmarks before recording the result.

For the ninth pass, confidence is scoped to the same side-channel-relaxed benchmark mode. The loopholes checked were accidental default-path changes, RM table mapping mistakes, RS degree-bound correctness, and substage/full-decode regression. The fixes/checks were: gate RM table lookup behind `HQC_RM_EXPAND_LUT=1`, leave default Hexagon and host decode passing, run host fast decode over the 16-fixture corpus, run Hexagon full-decode 1-iter and 10-iter benchmarks with the fastest flags, and rerun the affected RM expand, RS roots, and RS error-values substage benchmarks.

For the tenth pass, confidence is scoped to the side-channel-relaxed full-decode benchmark only. The loopholes checked were accidental default-path changes, fused RM semantic drift, substage benchmark contamination, and regression against the ninth-pass fastest result. The fixes/checks were: gate the fused path behind `HQC_RM_FUSED_FAST=1`, leave the existing expand/Hadamard/peak helpers and substage benchmark path unchanged, run default host decode over the 16-fixture corpus, run default Hexagon 1-iter decode, and run Hexagon 1-iter and 10-iter corpus benchmarks with `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, and `HQC_RM_FUSED_FAST=1`.

For the eleventh pass, confidence is scoped to the side-channel-relaxed RS fast path. The loopholes checked were inversionless-BM normalization cost, stale high-degree `z` coefficients, incorrect Forney denominator scaling, benchmark helper signature drift, accidental default-path changes, and regression against the tenth-pass fastest result. The fixes/checks were: keep inversionless BM as a rejected strategy for this software/GF-LUT setting, keep the default CT-oriented ELP and z/error-value paths unchanged, clear unused fast `z` coefficients, pass `sigma` explicitly into the error-value helper, verify both default and fast host decode over the 16-fixture corpus, run default Hexagon 1-iter decode, rerun the affected RS ELP/z/error-values substage benchmarks, and rerun Hexagon full-decode 1-iter and 10-iter fastest benchmarks.

For the twelfth pass, confidence is scoped to the side-channel-relaxed Hexagon HVX roots experiment. The loopholes checked were support-point ordering, invalid lanes 46..63, table initialization overhead, GF table versus HVX arithmetic tradeoff, accidental default-path changes, and full-decode regression. The fixes/checks were: precompute powers for exactly `gf_exp[PARAM_GF_MUL_ORDER - i]`, ignore lanes beyond `PARAM_N1`, gate the path behind `HQC_RS_ROOTS_HVX=1`, keep scalar Chien as the default, run host fast decode, run default Hexagon 1-iter decode, rerun RS roots substage 1-iter and 10-iter with the flag, and rerun Hexagon full-decode 1-iter and 10-iter fastest benchmarks with `HQC_RS_ROOTS_HVX=1`.

For the thirteenth pass, confidence is scoped to software constant-time behavior, correctness on the 16-fixture corpus, and Hexagon simulator Pcycles. It is not a formal physical side-channel proof. The loopholes checked were accidental data-dependent GF/RM tables in the default path, degree-dependent HVX roots, coefficient-dependent HVX roots branches, secret-indexed RM peak-value loads, implementation-defined signed-shift masks, fastest-path regression, and accidental promotion of branchy RS code. The fixes/checks were: keep `HQC_GF_LUT_MUL=0` in the default scripts, evaluate all `PARAM_DELTA + 1` coefficients in default `compute_roots_hvx`, keep the branchy degree-bound roots only under `HQC_RS_FAST_NON_CT=1`, recover RM peak sign with vector compare/mux/reduction in the default path, replace several default masks with unsigned helpers, keep the fused RM indexed load only under `HQC_RS_FAST_NON_CT=1`, run host decode, run default Hexagon 1-iter and 10-iter full-decode benchmarks, rerun RS ELP/roots/error-values substage 1-iter and 10-iter benchmarks, and rerun the fastest non-CT 1-iter and 10-iter benchmarks to confirm it remains 58,602 Pcycles/decode.
