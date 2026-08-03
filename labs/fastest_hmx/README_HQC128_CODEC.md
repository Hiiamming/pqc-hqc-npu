# HQC-128 Concatenated Codec: Hexagon HVX Intrinsic Variant

## HMX-32 / HVX-Tail Baseline

This experimental lab batches the first 32 Reed-Muller blocks into one HMX
matrix multiply and keeps the remaining 14 HQC-128 blocks on the HVX path.
The 128x128 Hadamard generator is cached once in VTCM as sixteen 32x32 WH
tiles. Each decode packs 32 expanded vectors as four AH tiles, runs four
output-row tiles, and unpacks the transform before the existing HVX peak
reduction. Set `HQC_RM_HMX_BATCH=0` on the HQC-128 Hexagon scripts to disable
the HMX batch and measure the HVX fallback.

The real-device FastRPC baseline requires the HexKL micro helper used to
configure HMX accumulator readback:

```sh
bash ../hmx-tutorial/ch05-hmx/install_hexkl.sh

ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
HEXAGON_ARCH=v75 \
HQC_PARAM_LEVEL=128 \
HQC_DEFAULT_BENCH_ITERS=125 \
HQC_PROJECT_DIR="$PWD/labs/fastest_hmx" \
HQC_RM_HMX_BATCH=1 \
HQC_RM_HMX_DEVICE=1 \
bash fastrpc/hqc/build_android_gcc_bionic.sh
```

This remains an experimental performance baseline. H2 simulator transforms
match the HVX reference exactly after the simulator readback bias adjustment.
On real HMX hardware, f16 accumulation is approximate: the full deterministic
decode fixture set passes because Reed-Solomon corrects the observed RM
differences, but RM-byte parity against the HVX path does not pass. Do not use
this variant as an exact drop-in replacement without a broader correctness
campaign.

This document is the handoff note for `hqc_lab_insintric`. It explains what changed from `hqc_lab_scalar`, why each optimization pass exists, where the code lives, which paths are default, and which paths are benchmark-only.

Short version: `hqc_lab_scalar` remains the scalar correctness baseline. `hqc_lab_insintric` moves the HQC-128 concatenated decoder toward Hexagon HVX and adds several side-channel-relaxed fast paths for benchmarking. The fastest measured configuration at the end of pass 12 is:

```sh
HQC1_BENCH_ITERS=10 \
HQC_RS_FAST_NON_CT=1 \
HQC_GF_LUT_MUL=1 \
HQC_RM_EXPAND_LUT=1 \
HQC_RM_FUSED_FAST=1 \
HQC_RS_ROOTS_HVX=1 \
bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh
```

Measured on the 16-fixture corpus:

| Iters | Fixtures | Result | Total insns | Total Pcycles |
| ---: | ---: | --- | ---: | ---: |
| 1 | 16 | PASS | 3,353,940 | 4,417,002 |
| 10 | 16 | PASS | 9,812,288 | 12,855,702 |

Estimated decode cost:

```text
(12,855,702 - 4,417,002) / (9 * 16) = 58,602 Pcycles/decode
```

This fastest configuration is benchmark-only. It intentionally uses branchy RS logic and data-dependent lookup tables because side-channel behavior was temporarily out of scope during the speed search. The default intrinsic build is more conservative: HVX syndrome computation is enabled, while branchy RS, GF tables, RM LUT expansion, fused RM, and HVX roots are all opt-in.

## Pass Summary

| Pass | Main idea | Resulting code path |
| ---: | --- | --- |
| 1 | Split scalar and intrinsic labs; add HVX RM Hadamard and peak reduction. | `reed_muller.c`: `hadamard_hvx`, `find_peaks_hvx` |
| 2 | Remove scalar even/odd gather from the HVX Hadamard transform. | `hadamard_hvx` uses HVX deal/deinterleave |
| 3 | Preserve the scalar RM peak tie-break exactly in vector logic. | `find_peaks_hvx` uses index vectors and `vmin` |
| 4 | Add HVX RM expansion/summation and stage benchmark support. | `expand_and_sum_hvx`; `hqc1_decode_stage_bench.c` |
| 5 | Replace default GF multiply with fixed-flow hardware-style `xtime`/xor and keep a GF LUT experiment. | `gf.c`: `gf_mul_hwstyle`, `HQC_GF_LUT_MUL` |
| 6 | Vectorize RS syndrome computation with HVX. | `compute_syndromes_hvx` |
| 7 | Target RS/RM bottlenecks: packed RM expand, shortened Chien roots, branchy fast RS experiments. | `compute_elp`, `compute_roots`, `compute_error_values` |
| 8 | Make GF LUT mode faster with a full 256x256 multiply table and LUT inverse. | `gf_mul_table`, `gf_inverse` |
| 9 | Add RM expansion LUT and degree-bound fast RS roots/error-values. | `HQC_RM_EXPAND_LUT`, actual `degree` in the RS fast path |
| 10 | Fuse RM expand, Hadamard, and peak inside full decode. | `rm_decode_one_hvx_fast`, `HQC_RM_FUSED_FAST` |
| 11 | Tighten branchy RS algebra: degree-bound BM update, degree-bound `z`, derivative denominator. | `compute_elp`, `compute_z_poly`, `compute_error_values` |
| 12 | Add HVX Chien root evaluation across 46 shortened RS support positions. | `compute_roots_hvx`, `HQC_RS_ROOTS_HVX` |

## Directory Map

`hqc_lab_scalar` is the portable scalar baseline. `hqc_lab_insintric` keeps the same broad source layout but adds HVX-specific code, benchmark fixtures, and opt-in fast flags.

Important files in `hqc_lab_insintric`:

| File | Role |
| --- | --- |
| `src/ref/reed_muller.c` | RM(1,7) encode/decode, scalar fallback, HVX Hadamard, HVX expansion, HVX peak, RM LUT, fused RM fast path |
| `src/ref/reed_solomon.c` | RS encode/decode, HVX syndrome, scalar/HVX Chien roots, branchy fast BM, degree-bound `z`, derivative error values |
| `src/ref/gf.c` | GF(2^8) arithmetic, fixed-flow hardware-style multiply, optional full GF table multiply/inverse |
| `src/ref/hqc-1/reed_solomon.h` | RS public prototypes plus substage benchmark hooks |
| `src/ref/hqc-1/parameters.h` | Minimal HQC-128 codec parameters, including `PARAM_N1 = 46`, `PARAM_DELTA = 15`, `PARAM_M = 8` |
| `demos/hqc1_decode_bench.c` | Full decode benchmark over the generated 16-fixture corpus |
| `demos/hqc1_decode_stage_bench.c` | Coarse RM-vs-RS benchmark |
| `demos/hqc1_decode_substage_bench.c` | Fine-grained RM/RS substage benchmark |
| `scripts/run_hqc1_decode_bench_hexagon.sh` | Full Hexagon decode benchmark and feature flags |
| `scripts/run_hqc1_decode_substage_bench_hexagon.sh` | Substage Hexagon benchmark and feature flags |
| `scripts/run_hqc1_decode_bench_host.sh` | Host correctness/benchmark build for default and fast modes |
| `fixtures/hqc1_decode_fixture.*` | Generated 16-case deterministic decode corpus |

## What Changed From `hqc_lab_scalar`

### 1. RM Decode: Scalar Loops to HVX Vector Lanes

In the scalar code, RM decode performs three conceptual steps for each of the `PARAM_N1 = 46` RS bytes:

1. Expand and sum the repeated RM codeword copies.
2. Run the 7-pass RM(1,7) Hadamard transform.
3. Find the transform peak to recover one byte.

The intrinsic build keeps the same algorithmic shape, but each expensive step has an HVX version.

#### `reed_muller.c`: HVX Includes and Tables

Compared with scalar, the intrinsic file includes:

```c
#include <hexagon_protos.h>
#include <hexagon_types.h>
```

It also adds aligned tables:

- `rm_index_lo[64]`, `rm_index_hi[64]`: lane indices for vector peak tie-break.
- `rm_expand3_nibble_table[4096]`: optional benchmark-only table for `HQC_RM_EXPAND_LUT=1`.

The RM LUT is specific to HQC-128 because `MULTIPLICITY = ceil(PARAM_N2 / 128) = 3`. A compile-time guard rejects the LUT/fused paths if that assumption changes.

#### `hadamard_hvx`

Scalar Hadamard repeatedly gathers even/odd elements:

```text
dst[i]       = src[2i] + src[2i+1]
dst[i + 64] = src[2i] - src[2i+1]
```

The first HVX pass had the right arithmetic but still paid for a scalar gather pattern. The optimized HVX version loads two 64-lane halfword vectors, uses `Q6_W_vdeal_VVR` and `Q6_Vh_vdeal_Vh` to form even/odd vectors, then performs vector halfword add/sub.

Intuition: the Hadamard butterfly is ideal for SIMD once lanes are arranged correctly. The main trick is not the add/sub; it is avoiding scalar lane reshuffling.

#### `expand_and_sum_hvx`

Scalar expansion writes one halfword per RM bit. The intrinsic path processes four bits at a time:

```text
nibble -> four 16-bit lanes
```

The helper `expand_nibble_to_u16()` maps a 4-bit nibble into packed 16-bit lane bits inside a `uint64_t`. For the three repeated RM copies, the intrinsic path sums three packed values and writes one 64-bit word.

With `HQC_RM_EXPAND_LUT=1`, the three nibbles are combined into one 12-bit index:

```text
index = n0 | (n1 << 4) | (n2 << 8)
```

The LUT output is already the sum of all three repetitions for four lanes. This is data-dependent and benchmark-only, but it is much faster in speed mode.

#### `find_peaks_hvx`

Scalar peak search scans 128 values and keeps the smallest index among values with maximal absolute value. The HVX path preserves that tie-break:

1. Load two 64-lane vectors.
2. Compute absolute values.
3. Reduce max with `vmax` and rotates.
4. Compare every lane against the max.
5. Select lane indices or sentinel `0x7fff`.
6. Reduce min index with `vmin`.
7. Set bit 7 if the signed peak is positive.

This matters because RM decoding uses the low 7 bits as the peak position and bit 7 as the sign bit. A vector implementation that picks a different equal peak could decode a different byte.

#### `rm_decode_one_hvx_fast`

This pass 10 path is enabled only by:

```text
HQC_RM_FUSED_FAST=1
```

It fuses:

```text
expand -> Hadamard -> half-Hadamard correction -> peak
```

inside one function. This avoids helper boundaries and some stack handoff between the generic expand/Hadamard/peak functions. The win was narrow but measurable, so it stayed gated as a benchmark-only path.

### 2. GF Arithmetic: Scalar Carryless Multiply to Hardware-Style and Table Modes

The scalar baseline has a generic GF(2^8) multiply using carryless multiplication and reduction. The intrinsic lab adds two important alternatives in `src/ref/gf.c`.

#### Default Intrinsic GF Multiply

`HQC_USE_GF_HWSTYLE_MUL=1` uses a fixed-flow `xtime`/xor shape:

```text
acc = xtime(acc)
if bit of b is set: acc ^= a
```

The implementation uses masks instead of branches. This shape was inspired by the HQC hardware multiplier in `git/pqc-hqc-hardware/hardware/decap/gfmul.v`. It keeps arithmetic fixed-flow and avoids lookup tables in the default path.

#### Benchmark GF Table Mode

`HQC_GF_LUT_MUL=1` initializes:

```text
gf_mul_table[256][256]
```

and uses `gf_log`/`gf_exp` directly for inversion. This is fast, but the memory access depends on GF operands. It is therefore benchmark-only unless the threat model explicitly accepts that leakage.

The table-driven idea also matches the direction in `git/2512.12904v1.pdf`, which describes replacing repeated GF products in RS encode/syndrome code with precomputed lookup tables. In this lab, the most profitable measured table path was full GF multiply/inverse plus RM expansion LUT, rather than only syndrome nibble tables.

### 3. RS Syndrome: Scalar Double Loop to HVX Lanes

Scalar syndrome computation is:

```math
S_i = c_0 \oplus \bigoplus_{j=1}^{N_1-1} c_j \cdot \alpha^{i j}
```

for `i = 0..2*PARAM_DELTA-1`.

The intrinsic path transposes the power table into:

```c
alpha_ji_pow[PARAM_N1 - 1][64]
```

so one HVX vector holds all syndrome constants for a fixed received byte position `j`. Then each received byte `c_j` is multiplied by the whole vector of constants with `gf_mul_scalar_by_vec_hvx()`, and all products are XOR-accumulated.

Intuition: one RS byte contributes to every syndrome. HVX is useful because those 30 contributions are independent lanes.

This path is default in the Hexagon intrinsic scripts through:

```text
HQC_HVX_RS_SYNDROME=1
```

### 4. RS Berlekamp-Massey: Fixed-Flow Default and Branchy Fast Mode

The default `compute_elp()` remains masked and fixed-flow. It keeps the side-channel-oriented behavior of the scalar implementation.

The benchmark fast path is enabled by:

```text
HQC_RS_FAST_NON_CT=1
```

It uses standard branchy Berlekamp-Massey and returns the actual degree of the error locator polynomial:

```math
\sigma(x) = 1 + \sigma_1 x + \cdots + \sigma_d x^d
```

Pass 11 tightened this fast path by tracking the actual degree of the auxiliary polynomial `b`. Before that pass, the update loop still ran to `PARAM_DELTA`:

```text
sigma[i] ^= dd * b[i - m]
```

even when high coefficients of `b` were zero. Now the update stops at:

```text
min(PARAM_DELTA, m + deg_b)
```

This is not a new decoder; it is the same branchy BM with less useless work.

Why not inversionless BM from `git/wu2015.pdf`? Wu 2015 is useful conceptually because it discusses inversionless BM, dynamic stopping, and error evaluation using locator/auxiliary polynomials. But in this lab's fastest mode, `HQC_GF_LUT_MUL=1` makes inversion cheap. Switching to inversionless BM would avoid inversions but introduce more polynomial scaling/update work. For `PARAM_DELTA = 15`, that tradeoff looked weak, so the implemented pass kept branchy BM and optimized actual work bounds.

### 5. RS Roots: Shortened Chien to HVX Chien

The intrinsic path uses shortened-support Chien search for root finding. Instead of evaluating the locator over all 256 field elements, it checks only the public shortened RS support:

```math
x_i = \alpha^{-i}, \quad i = 0,\dots,45
```

and marks an error when:

```math
\sigma(x_i) = 0
```

In scalar form, this is Horner evaluation:

```text
acc = sigma[degree]
for j = degree..1:
    acc = acc * x_i ^ sigma[j - 1]
```

Pass 12 adds `compute_roots_hvx()` behind:

```text
HQC_RS_ROOTS_HVX=1
```

It precomputes:

```math
P[j][i] = x_i^j
```

for `j = 0..PARAM_DELTA` and `i = 0..45`, padded to 64 HVX lanes. Then it evaluates:

```math
\sigma(x_i) = \bigoplus_{j=0}^{d} \sigma_j x_i^j
```

for all 46 support points in one HVX vector.

This is the best current root path:

| Roots path | Estimated cost |
| --- | ---: |
| Scalar degree-bound Chien, pass 9 | 9,271 Pcycles/decode |
| HVX Chien, pass 12 | 2,132 Pcycles/decode |

`git/2312.02579v1.pdf` proposes a modulus-search replacement for Chien search. It is relevant as a root-finding paper, but not a good fit here. Its advantage appears for full-field searches and large locator degrees. HQC-128 uses a shortened support of only 46 positions and `PARAM_DELTA = 15`; the HVX shortened Chien path is already smaller and measured faster.

### 6. RS `z(x)` and Error Values

The scalar/default `compute_z_poly()` keeps masked loops to `PARAM_DELTA`. The fast path now computes only to the actual locator degree and clears unused coefficients.

The error-value step uses Forney-style evaluation. In characteristic 2, the formal derivative keeps only odd-degree coefficients:

```math
\sigma'(x) = \sigma_1 + \sigma_3 x^2 + \sigma_5 x^4 + \cdots
```

The fast path evaluates:

```math
z(\beta_i^{-1})
```

and uses:

```math
\sigma'(\beta_i^{-1})
```

as the denominator, instead of explicitly multiplying:

```math
\prod_{k \ne i} (1 \oplus \beta_i^{-1}\beta_k)
```

This follows the same derivative/odd-locator observation used in Reed-Solomon error-evaluation literature, including the discussion around Forney/Horiguchi-Koetter style formulas in `git/wu2015.pdf`.

### 7. Benchmarks and Fixtures

The scalar lab originally used narrower testing. The intrinsic lab now has a generated 16-case decode fixture corpus:

```text
fixtures/hqc1_decode_fixture.c
fixtures/hqc1_decode_fixture.h
```

The corpus covers deterministic random messages and RS-symbol error counts from 0 through `PARAM_DELTA`. Every full decode benchmark decodes all 16 fixtures per iteration.

The important benchmark entry points are:

```sh
bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_host.sh
bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh
bash hqc_lab_insintric/scripts/run_hqc1_decode_substage_bench_hexagon.sh
```

The substage benchmark is compiled with:

```text
HQC_ENABLE_SUBSTAGE_BENCH=1
```

and exposes wrappers from `reed_solomon.h` so individual RS/RM stages can be measured without changing production decode logic.

Important warning: do not run two benchmark scripts that write the same output binary in parallel. The output binaries live under `hqc_lab_insintric/build/`; parallel runs can cause `Text file busy` or overwrite a measured binary before the simulator starts.

## Optimization Deep Dive

This section is written for code handoff: each item states which stage is being optimized, which functions contain the logic, what the original algorithm was, what the intrinsic/fast version changed, and why the new version is faster on Hexagon HVX. Flags ending in `FAST`, `LUT`, or `NON_CT` should be read as benchmark paths, not production-safe side-channel paths.

### A. RM Expansion: Bit-Unpack Scalar to Packed Nibble/LUT

**Code location:** `src/ref/reed_muller.c`, specifically `expand_nibble_to_u16()`, `expand_and_sum_hvx()`, `rm_expand3_nibble_table[]`, and the fast caller in `reed_muller_decode()`.

HQC-128 RM decode reads an RM codeword repeated `MULTIPLICITY = 3` times. Before Hadamard, each RM bit must be expanded into a 16-bit value and summed across the three copies:

```math
expanded[i] = bit(c_0, i) + bit(c_1, i) + bit(c_2, i)
```

The scalar baseline does this by extracting each bit, writing each `uint16_t`, then having Hadamard read `expanded[128]` back. The cost comes from many tiny operations: bit extract, cast to halfword, store, and repeat 128 times per RS byte.

The HVX/packed path in `expand_and_sum_hvx()` takes a different shape. Instead of processing one bit at a time, it takes a 4-bit nibble:

```text
b3 b2 b1 b0
```

and `expand_nibble_to_u16()` turns it into four 16-bit lanes packed into one `uint64_t`:

```text
000b0 000b1 000b2 000b3
```

For the three RM copies, the code sums three packed values and stores once. Four RM positions are therefore processed as one 64-bit unit, reducing unpack/store work from 128 scalar halfwords to 32 packed stores.

With `HQC_RM_EXPAND_LUT=1`, the code goes further: the three nibbles from the three copies are joined into one 12-bit index:

```text
idx = n0 | (n1 << 4) | (n2 << 8)
```

`rm_expand3_nibble_table[idx]` directly returns four halfwords that already contain the sum of all three copies. The inner loop no longer needs packed addition; it only loads bytes, extracts nibbles, looks up the LUT, and stores the packed output. This is why RM expand drops sharply in benchmarks. The tradeoff is data-dependent LUT access, so this path remains benchmark-only until a side-channel review accepts it.

Why it is better: RM expansion is small but hot, running `46` times per decode. Reducing small stores/loads in this stage directly affects Pcycles/decode. The packed nibble format also matches the Hadamard input layout because the output is still `uint16_t[128]`.

### B. RM Hadamard: Same Butterfly, HVX Lane Shuffle

**Code location:** `src/ref/reed_muller.c`, function `hadamard_hvx()`.

RM(1,7) decode uses a Walsh-Hadamard transform over 128 values. Each transform pass is a set of butterflies:

```text
u = a + b
v = a - b
```

The scalar baseline loops over the array and picks even/odd pairs by index. With 128 points and 7 passes, the cost is not only add/sub; it is repeatedly reading even/odd patterns and writing new patterns.

`hadamard_hvx()` keeps the exact butterfly formula but loads two 64-lane halfword vectors. The important part is HVX deal/deinterleave:

```c
Q6_W_vdeal_VVR(...)
Q6_Vh_vdeal_Vh(...)
```

which turns two contiguous vectors into `even` and `odd` vectors. Once lanes are in the right places, add/sub becomes:

```text
sum  = even + odd
diff = even - odd
```

using vector halfword operations.

Why it is better: one HVX instruction processes 64 halfword lanes. Hadamard is a clean SIMD pattern if scalar gather is removed. Pass 2 fixed exactly this by using HVX lane permutation instead of scalar even/odd gather. Arithmetic and reshuffle stay in vector registers, limiting round trips through stack/memory.

Correctness point: the butterfly transform itself is unchanged. The optimization only changes how lanes are arranged before computing `a+b` and `a-b`. The output order must match scalar, so the code keeps the pass-by-pass transform structure rather than switching to a harder-to-verify transform variant.

### C. RM Peak Search: Vector Max With Scalar-Compatible Tie-Break

**Code location:** `src/ref/reed_muller.c`, function `find_peaks_hvx()`, tables `rm_index_lo[]` and `rm_index_hi[]`.

After Hadamard, RM decode selects:

```math
j = \arg\max_i |H_i|
```

The low 7 bits of the output byte are the peak index `j`; bit 7 is set from the sign of the peak value. If multiple peaks tie, the scalar implementation chooses the smallest index. This tie-break matters because a vector implementation that chooses a different equal peak could decode a different byte.

`find_peaks_hvx()` does the search in vector steps:

1. Load 128 halfwords into two 64-lane vectors.
2. Compute absolute values per lane.
3. Reduce max using rotates and `vmax`.
4. Compare every lane with the max.
5. Keep the lane index for matching lanes and use a large sentinel otherwise.
6. Reduce the minimum index using `vmin`.
7. Read the sign of the winning peak and set bit 7.

Why it is better: instead of 128 scalar branchy compare/update steps, the code performs 128 compares at once and reduces in vector registers. `rm_index_lo[]` and `rm_index_hi[]` exist so the tie-break has the same semantics as scalar: among equal peaks, the smaller index wins.

### D. RM Fused Path: Remove Store/Load Handoff Between Expand, Hadamard, and Peak

**Code location:** `src/ref/reed_muller.c`, function `rm_decode_one_hvx_fast()` and the selection branch in `reed_muller_decode()`. Enabled with `HQC_RM_FUSED_FAST=1`.

Before pass 10, full decode called three separate helpers:

```text
expand_and_sum_hvx()
hadamard_hvx()
find_peaks_hvx()
```

Each helper was algorithmically correct, but the boundaries had cost: parameters, stack temporaries, and materialized intermediates for the next helper to read. `rm_decode_one_hvx_fast()` combines the three steps for one RM block:

```text
load 3 RM copies -> packed/LUT expand -> Hadamard -> correction -> peak
```

Why it is better: this optimization does not change algorithmic complexity; it reduces overhead around the algorithm. Since the RM side runs 46 times per decode, small per-block overhead is still measurable. The pass 10 win is smaller than the LUT and HVX roots wins, but the fastest measured path improves when fused RM is enabled.

Tradeoff: the fused path is harder to read than the helper path and depends on `MULTIPLICITY = 3`, so it is gated as benchmark-only.

### E. GF Multiply: Fixed-Flow Hardware-Style and Table Mode

**Code location:** `src/ref/gf.c`, functions `gf_mul_hwstyle()`, `gf_mul_table_init()`, `gf_mul()`, and `gf_inverse()`.

GF(2^8) multiply is used across RS: syndrome, Berlekamp-Massey, root evaluation, `z(x)`, and error values. The scalar baseline uses generic multiply/reduce. The intrinsic lab has two directions.

**Default path:** `gf_mul_hwstyle()` uses a fixed 8-step `xtime`/xor shape:

```text
acc = 0
for bit in b:
    acc ^= a if bit is set
    a = xtime(a) mod 0x11d
```

The condition is masked instead of branched. This was inspired by the hardware GF multiplier in `git/pqc-hqc-hardware/hardware/decap/gfmul.v`: each step is shift, conditional xor with the polynomial, and xor-accumulate. This shape is compiler-friendly because control flow is fixed and the step count is small.

**Benchmark path:** `HQC_GF_LUT_MUL=1` creates:

```text
gf_mul_table[256][256]
```

Then `gf_mul(a,b)` is effectively a table lookup. `gf_inverse()` also uses log/exp tables directly. The precomputed-product direction matches the table-driven idea in `git/2512.12904v1.pdf`: replace repeated GF products with lookup tables. In this repo, a full GF table measured better than variants that only tabled syndrome work.

Why it is better: the RS fast path calls GF multiply heavily. When side-channel leakage is temporarily ignored, a 64 KiB table is a small cost compared with saving thousands of GF operations per decode. The clear tradeoff is operand-dependent memory access.

### F. RS Syndrome HVX: Vectorize Across Syndromes

**Code location:** `src/ref/reed_solomon.c`, table `alpha_ji_pow[][]`, helper `gf_mul_scalar_by_vec_hvx()`, and function `compute_syndromes_hvx()`.

RS syndrome has the form:

```math
S_i = c_0 \oplus \bigoplus_{j=1}^{N_1-1} c_j \alpha^{ij},
\quad i = 0,\dots,2\delta-1
```

For HQC-128, `N_1 = 46` and `2*PARAM_DELTA = 30`. A scalar double loop typically thinks in `i` then `j`: compute each syndrome separately. On HVX, the more natural direction is vectorizing across `i`: one codeword byte `c_j` contributes to all 30 syndromes at once.

Therefore `alpha_ji_pow[j][lane]` is transposed so one vector holds:

```text
alpha^(0*j), alpha^(1*j), ..., alpha^(29*j)
```

Then `gf_mul_scalar_by_vec_hvx(c_j, powers)` multiplies the same scalar `c_j` with all lanes and XORs into the syndrome accumulator vector.

Why it is better: each `c_j` creates 30 independent GF products. This is a good SIMD shape: same operation, independent data, independent output lanes. Instead of 45*30 scalar multiply/update operations, the code iterates over 45 RS positions and updates the whole syndrome vector. This path is the default Hexagon intrinsic path through `HQC_HVX_RS_SYNDROME=1` because it does not require new secret-dependent branching.

### G. Berlekamp-Massey Fast Path: Reduce Work by Actual Degree

**Code location:** `src/ref/reed_solomon.c`, function `compute_elp()`, branch `#if HQC_RS_FAST_NON_CT`.

BM finds the error locator polynomial:

```math
\sigma(x)=1+\sigma_1x+\cdots+\sigma_dx^d
```

from syndromes. Each step computes a discrepancy:

```math
\Delta_\mu = S_\mu \oplus \sum_{i=1}^{d}\sigma_i S_{\mu-i}
```

If the discrepancy is nonzero, the locator is updated with a shifted auxiliary polynomial `B(x)`. The default project path keeps masked/fixed-flow behavior for side-channel discipline. The fast path returns to classic branchy BM: discrepancy = 0 skips the update, and discrepancy != 0 updates only the needed range.

Pass 11 optimized one specific point: the update previously tended to run to `PARAM_DELTA` even when auxiliary polynomial `b` had a smaller actual degree. The code now tracks `deg_b` and limits the update:

```text
upper = min(PARAM_DELTA, m + deg_b)
```

Why it is better: `PARAM_DELTA = 15` is small, but BM is on the hot RS path and each update needs GF multiply/xor. When the real error count is below the bound or `b` is not full-degree, many high coefficients are certainly zero. Skipping them does not change the result because those terms contribute zero to the polynomial update.

Paper connection: `git/wu2015.pdf` discusses inversionless BM, dynamic stopping, and error-evaluation variants. After testing in this repo, full inversionless BM was not chosen because with `HQC_GF_LUT_MUL=1`, inversion is cheap; switching to inversionless risks adding more polynomial scaling/update work. The useful idea was doing work according to the actual locator/auxiliary degree, which was applied to BM update and later stages.

### H. RS Root Finding: Use Shortened HVX Chien

**Code location:** `src/ref/reed_solomon.c`, functions `compute_roots()`, `compute_roots_hvx()`, and table `rs_support_powers[][]`.

The decoder needs error positions by finding roots of the locator:

```math
\sigma(x_i)=0
```

HQC-128 RS here is a shortened code with only 46 support positions:

```math
x_i=\alpha^{-i},\quad i=0,\dots,45
```

Pass 9 therefore switched to shortened Chien search: evaluate only 46 points instead of the full field. Pass 12 then vectorized the evaluation:

```math
\sigma(x_i)=\bigoplus_{j=0}^{d}\sigma_j x_i^j
```

`rs_support_powers[j][i]` precomputes `x_i^j`, padded to 64 HVX lanes. `compute_roots_hvx()` loads vector powers for each degree `j`, multiplies all lanes by scalar `sigma[j]`, XOR-accumulates, then compares the accumulator with zero to create an error mask for the 46 valid lanes.

Why it is better: this is one of the clearest wins. Full/additive root search and scalar Horner both lose because they do not exploit the fact that support has only 46 points. HVX Chien turns "evaluate 46 points" into "evaluate 46 lanes" inside one vector loop over degree. Benchmark result:

| Roots path | Estimated cost |
| --- | ---: |
| Scalar degree-bound Chien, pass 9 | 9,271 Pcycles/decode |
| HVX Chien, pass 12 | 2,132 Pcycles/decode |

About `git/2312.02579v1.pdf`: modulus search is a relevant alternative to Chien search for full-field search or larger degrees. In the current setting, short support (`46`) and low degree (`<=15`) make HVX shortened Chien simpler, lower-overhead, and measured faster. The paper is noted as an alternative, but not the best implementation direction for the current benchmark.

### I. `z(x)` and Error Values: Degree Bounds and Formal Derivative

**Code location:** `src/ref/reed_solomon.c`, functions `compute_z_poly()` and `compute_error_values()`.

After the locator is known, the decoder computes evaluator `z(x)` and error values. The simple form is:

```math
z(x) = \sigma(x) S(x) \bmod x^{2\delta}
```

The default path still keeps fixed loops to `PARAM_DELTA`. The fast path uses the actual locator `degree` to avoid computing coefficients that cannot matter. Coefficients outside the bound are cleared so the caller/benchmark sees clean output.

For error values, the old approach computes the denominator as a product over other error positions:

```math
\prod_{k \ne i}(1 \oplus \beta_i^{-1}\beta_k)
```

Pass 11 changed the fast path to use the formal derivative of the locator. In characteristic 2, the formal derivative drops all even-degree coefficients:

```math
\sigma'(x)=\sigma_1+\sigma_3x^2+\sigma_5x^4+\cdots
```

So the denominator can be computed by evaluating `sigma'(x)` at the correction point, instead of multiplying across all other roots.

Why it is better: with up to 15 errors, product-over-other-errors is roughly O(t^2) in the number of errors. The derivative denominator becomes an evaluation by locator degree and skips even degrees in characteristic 2. This matches Forney/Horiguchi-Koetter style formulas discussed in `git/wu2015.pdf`. In measured pass 11, the fast RS stages improved further:

| Stage | Before pass 11 | After pass 11 |
| --- | ---: | ---: |
| BM/ELP | 11,220 | 7,879 |
| `z(x)` | 2,965 | 1,141 |
| error values | 7,217 | 6,004 |

Units are estimated Pcycles/decode from the substage benchmark. The tradeoff remains that this is a branchy/non-constant-time fast path.

### J. Benchmark Wrappers: Measure the Right Stage

**Code location:** `demos/hqc1_decode_bench.c`, `demos/hqc1_decode_stage_bench.c`, `demos/hqc1_decode_substage_bench.c`, and wrappers at the end of `src/ref/reed_solomon.c` under `HQC_ENABLE_SUBSTAGE_BENCH`.

Optimization in this project used the loop:

```text
full decode benchmark -> coarse stage benchmark -> substage benchmark -> patch -> verify corpus
```

If only full decode is observed, it is easy to optimize a stage that is no longer the bottleneck. The repo therefore adds wrappers:

```c
hqc_rs_bench_compute_elp(...)
hqc_rs_bench_compute_roots(...)
hqc_rs_bench_compute_z_poly(...)
hqc_rs_bench_compute_error_values(...)
```

and separate RM expand/Hadamard/peak benchmarks. This is why later passes focused on RM expand, RS roots, BM degree bounds, and derivative error values instead of continuing to touch code that was no longer hot.

Why it is better: this is not a runtime optimization directly; it is workflow optimization. It gives every change a stage-specific measurement and avoids adding complexity that does not improve full decode. Only results that both improve Pcycles and pass the 16-fixture corpus should be recorded.

## Feature Flags

| Flag | Default | Scope | Meaning |
| --- | --- | --- | --- |
| `HQC_USE_HVX_INTRINSICS` | set by Hexagon scripts | Hexagon | Enable HVX code blocks |
| `HQC_USE_GF_HWSTYLE_MUL` | `1` unless GF LUT is enabled | default intrinsic | Fixed-flow `xtime`/xor GF multiply |
| `HQC_HVX_RS_SYNDROME` | `1` | default intrinsic | Use HVX syndrome computation |
| `HQC_RS_FAST_NON_CT` | `0` | benchmark-only | Branchy RS BM/error-value fast mode |
| `HQC_GF_LUT_MUL` | `0` | benchmark-only | Full GF(256) multiplication table and LUT inverse |
| `HQC_RM_EXPAND_LUT` | `0` | benchmark-only | RM 3-nibble expansion/sum LUT |
| `HQC_RM_FUSED_FAST` | `0` | benchmark-only | Full-decode fused RM block path |
| `HQC_RS_ROOTS_HVX` | `0` | benchmark-only | HVX Chien search over shortened RS support |

## Pass Results at a Glance

The repository root README contains the detailed tables. The key corpus-estimated decode costs are:

| Variant | Estimated Pcycles/decode |
| --- | ---: |
| Scalar | 953,767 |
| Seventh pass CT | 396,505 |
| Seventh pass fast non-CT + opt-in GF LUT | 149,482 |
| Eighth pass fast non-CT + GF table LUT | 111,361 |
| Ninth pass fast non-CT + GF table LUT + RM LUT | 67,034 |
| Tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 66,620 |
| Eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 60,834 |
| Twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 58,602 |

The fastest path is about 93.9% lower Pcycles/decode than the scalar baseline by this benchmark estimate.

## Paper and Design Notes

The following local documents informed the work:

- `git/pqc-hqc-hardware/hardware/decap/gfmul.v`: shaped the fixed-flow `xtime`/xor GF multiplier used by default in `gf.c`.
- `git/2512.12904v1.pdf`: supports the table-driven RS direction. The lab used that idea most directly for the full GF multiply table and indirectly for thinking about table-driven syndrome work.
- `git/wu2015.pdf`: explains inversionless BM and derivative/odd-locator style error evaluation. The implemented pass 11 used the derivative denominator idea, but rejected full inversionless BM as a poor software tradeoff for small `PARAM_DELTA` with cheap GF LUT inverse.
- `git/optimizing hqc using frobenius additive fft.pdf` and `git/2025-1939.pdf`: mostly target HQC polynomial multiplication, not this RS/RM concatenated decoder hot path. They were useful context but did not directly replace the RS/RM decoder stages here.
- `git/2312.02579v1.pdf`: proposes modulus search as a Chien-search alternative. It targets full-field and larger-degree root search; HQC-128 shortened support plus HVX Chien measured better for this lab.
- `git/hexagon_v79_hvx.pdf`: general HVX context for 128-byte vector programming, vector predicates, and lane-oriented arithmetic.

## Correctness and Side-Channel Scope

Correctness checks used:

```sh
bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_host.sh
HQC_RS_FAST_NON_CT=1 HQC_GF_LUT_MUL=1 HQC_RM_EXPAND_LUT=1 \
  bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_host.sh
HQC1_BENCH_ITERS=1 \
  bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh
```

Each pass also used targeted Hexagon 1-iter/10-iter full and substage runs as needed.

Security scope:

- The default intrinsic build is the side-channel-oriented baseline in this lab.
- Anything using `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, `HQC_RM_FUSED_FAST=1`, or `HQC_RS_ROOTS_HVX=1` should be treated as benchmark-only until a separate side-channel review accepts it.
- The fastest path is not a production recommendation. It is a measured performance endpoint under the temporary instruction to ignore side-channel attacks.

## Recommended Next Handoff Steps

1. Keep `hqc_lab_scalar` unchanged as the correctness reference.
2. Use `hqc_lab_insintric` for Hexagon performance experiments.
3. When testing a new optimization, add a flag first, then run:
   - host default decode,
   - host fast decode if the flag affects fast mode,
   - Hexagon default 1-iter,
   - relevant Hexagon substage 1/10,
   - full Hexagon fastest 1/10.
4. Record only results that improve Pcycles and keep passing the 16-fixture corpus.
5. Do not promote benchmark-only flags to default without a side-channel decision.
