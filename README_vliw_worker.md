# VLIW / Worker-Pool Trial Notes

This file records the Hexagon DSP User Guide ideas that were actually applied
to the current `labs/fastest` and `fastrpc/hqc` paths, plus how to reproduce
the runs.

## Applied Methods

### 1. VLIW / Maximize Instructions Per Packet

Goal: expose more independent HVX work so the Hexagon scheduler can fill more
instruction slots per packet.

Implemented in:

- `labs/fastest/src/ref/reed_solomon.c`
- `labs/fastest/src/ref/reed_muller.c`

Changes:

- Reed-Solomon GF vector multiply now precomputes bit masks before the chained
  multiply steps. This reduces work inside the dependent `xtime` chain.
- Reed-Muller decode now processes two RM blocks together. The two Hadamard
  transforms are interleaved so independent HVX ops are available between
  dependent steps.

### 2. Understand And Reduce Stalls

Goal: reduce short dependency chains where each HVX instruction immediately
needs the previous result.

Implemented in:

- `labs/fastest/src/ref/reed_solomon.c`
- `labs/fastest/src/ref/reed_muller.c`

Changes:

- RS mask extraction is moved before the dependent GF accumulation chain.
- RM Hadamard work is paired across two blocks, so the scheduler can issue work
  from block B while block A has dependent results pending.

### 3. Optimize Multi-Threaded DSP Code

Goal: use DSP worker threads for independent decode jobs.

Implemented in:

- `fastrpc/hqc/dsp/hqc_dsp.c`
- `fastrpc/hqc/build.sh`
- `fastrpc/hqc/host/main.c`

Changes:

- Added optional `HQC_USE_WORKER_POOL=1` build mode.
- Added runtime `buffer-bench ... worker` mode.
- The DSP splits a batch of independent codewords into ranges.
- Parent DSP thread handles range 0.
- Other ranges are submitted to the Hexagon worker pool.
- Results are joined with a worker sync token.

### 4. Offload Tasks From One DSP Thread To Another

Goal: keep one FastRPC call, but offload independent decode ranges to other DSP
worker threads.

Implemented in:

- `hqc_decode_buffer_bench_worker()` in `fastrpc/hqc/dsp/hqc_dsp.c`

Important behavior:

- Input/output buffers are shared across ranges.
- Each worker writes to a disjoint message range.
- Each worker has local aligned decode scratch.
- HQC-256 initially failed on device with `FastRPC error=0x8000040d`, even at
  `iters=1 codeword_count=1`. The fix is to avoid creating a second pool per
  call and reduce stack pressure in the worker branch.

## Detailed Code Changes

### `labs/fastest/src/ref/reed_solomon.c`

Changed function:

- `gf_mul_scalar_by_vec_hvx()`

Old code shape:

```c
#define GF_MUL_VEC_STEP(bit)                                      \
    do {                                                          \
        HVX_Vector bbit = Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(b, (bit)), one); \
        HVX_Vector bit_mask = Q6_Vh_vsub_VhVh(zero, bbit);        \
        acc = gf_xtime_hvx(acc);                                  \
        acc = Q6_V_vxor_VV(acc, Q6_V_vand_VV(avec, bit_mask));    \
    } while (0)

GF_MUL_VEC_STEP(7);
GF_MUL_VEC_STEP(6);
...
GF_MUL_VEC_STEP(0);
```

Old behavior:

- Every bit step extracts one mask and immediately updates `acc`.
- The `acc = gf_xtime_hvx(acc)` operations form a dependency chain.
- Mask extraction sits inside that chain, so the compiler has less independent
  work to schedule.

New code shape:

```c
HVX_Vector mask7 = Q6_Vh_vsub_VhVh(zero, Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(b, 7), one));
HVX_Vector mask6 = Q6_Vh_vsub_VhVh(zero, Q6_V_vand_VV(Q6_Vuh_vlsr_VuhR(b, 6), one));
...
HVX_Vector mask0 = Q6_Vh_vsub_VhVh(zero, Q6_V_vand_VV(b, one));

#define GF_MUL_VEC_APPLY(mask)                                  \
    do {                                                        \
        acc = gf_xtime_hvx(acc);                                \
        acc = Q6_V_vxor_VV(acc, Q6_V_vand_VV(avec, (mask)));    \
    } while (0)

GF_MUL_VEC_APPLY(mask7);
GF_MUL_VEC_APPLY(mask6);
...
GF_MUL_VEC_APPLY(mask0);
```

New behavior:

- All bit masks are computed first.
- The dependent accumulation loop only does `xtime`, `and`, and `xor`.
- This matches the "reduce stalls" idea from the guide: move independent work
  out of a dependent chain.

### `labs/fastest/src/ref/reed_muller.c`

Added functions:

- `rm_hadamard_two_rows_hvx()`
- `rm_decode_two_hvx_fast()`

Changed function:

- `reed_muller_decode()`

Old decode loop:

```c
void reed_muller_decode(uint64_t *msg, const uint64_t *cdw) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    for (size_t i = 0; i < VEC_N1_SIZE_BYTES; i++) {
        message_array[i] = rm_decode_one_hvx_fast(&codeArray[i * MULTIPLICITY]);
    }
}
```

Old behavior:

- Decode one RM block at a time.
- Each block runs expand -> Hadamard -> fix -> peak.
- Hadamard has repeated dependent passes, so there is limited independent work
  inside one block.

New decode loop:

```c
void reed_muller_decode(uint64_t *msg, const uint64_t *cdw) {
    uint8_t *message_array = (uint8_t *)msg;
    rm_codeword_t *codeArray = (rm_codeword_t *)cdw;
    size_t i = 0;
    for (; i + 1 < VEC_N1_SIZE_BYTES; i += 2) {
        rm_decode_two_hvx_fast(message_array, codeArray, i);
    }
    for (; i < VEC_N1_SIZE_BYTES; i++) {
        message_array[i] = rm_decode_one_hvx_fast(&codeArray[i * MULTIPLICITY]);
    }
}
```

New paired Hadamard shape:

```c
#define RM_HADAMARD_TWO_PASS()                                  \
    do {                                                        \
        HVX_VectorPair adeal = Q6_W_vdeal_VVR(ahi, alo, 2);     \
        HVX_VectorPair bdeal = Q6_W_vdeal_VVR(bhi, blo, 2);     \
        HVX_Vector ae = Q6_Vh_vdeal_Vh(Q6_V_lo_W(adeal));       \
        HVX_Vector be = Q6_Vh_vdeal_Vh(Q6_V_lo_W(bdeal));       \
        HVX_Vector ao = Q6_Vh_vdeal_Vh(Q6_V_hi_W(adeal));       \
        HVX_Vector bo = Q6_Vh_vdeal_Vh(Q6_V_hi_W(bdeal));       \
        alo = Q6_Vh_vadd_VhVh(ae, ao);                          \
        blo = Q6_Vh_vadd_VhVh(be, bo);                          \
        ahi = Q6_Vh_vsub_VhVh(ae, ao);                          \
        bhi = Q6_Vh_vsub_VhVh(be, bo);                          \
    } while (0)
```

New behavior:

- Decode two independent RM blocks together.
- In each Hadamard pass, block A and block B work is interleaved.
- The scheduler has more independent HVX instructions available, which is the
  practical VLIW change here.

### `fastrpc/hqc/build.sh`

Changed purpose:

- Add worker-pool as an optional build feature.
- Normal builds still default to `HQC_USE_WORKER_POOL=0`.

New variables:

```sh
HQC_USE_WORKER_POOL="${HQC_USE_WORKER_POOL:-0}"
WORKER_POOL_ROOT="${WORKER_POOL_ROOT:-$HEXAGON_SDK_ROOT/libs/worker_pool}"
WORKER_POOL_INC_DIR="${WORKER_POOL_INC_DIR:-$WORKER_POOL_ROOT/inc}"
WORKER_POOL_LIB="${WORKER_POOL_LIB:-$WORKER_POOL_ROOT/prebuilt/hexagon_toolv19_v68/libworker_pool.a}"
```

New include/link setup:

```sh
worker_pool_inc_args=()
worker_pool_link_args=()
if [ "$HQC_USE_WORKER_POOL" = "1" ]; then
    if [ ! -f "$WORKER_POOL_INC_DIR/worker_pool.h" ]; then
        echo "ERROR: $WORKER_POOL_INC_DIR/worker_pool.h not found" >&2
        exit 1
    fi
    if [ ! -f "$WORKER_POOL_LIB" ]; then
        echo "ERROR: $WORKER_POOL_LIB not found" >&2
        exit 1
    fi
    worker_pool_inc_args=(-I "$WORKER_POOL_INC_DIR")
    worker_pool_link_args=("$WORKER_POOL_LIB")
fi
```

New DSP compile define:

```sh
-DHQC_USE_WORKER_POOL="$HQC_USE_WORKER_POOL"
```

Effect:

- Without `HQC_USE_WORKER_POOL=1`, the worker-pool header/library are not used.
- With `HQC_USE_WORKER_POOL=1`, the DSP skel links `libworker_pool.a`.

### `fastrpc/hqc/host/main.c`

Changed purpose:

- Add a host-visible runtime mode for worker-pool benchmarking.

Old usage accepted:

```text
direct | copy | l2fetch | vtcm
```

New usage accepts:

```text
direct | copy | l2fetch | vtcm | worker
```

Code added:

```c
#define HQC_BUFFER_MODE_WORKER_POOL 4
```

```c
if (strcmp(arg, "worker") == 0) {
    return HQC_BUFFER_MODE_WORKER_POOL;
}
```

Effect:

```sh
./hqc_host buffer-bench rpcmem-cached worker 100 16
```

now selects the DSP worker-pool path.

### `fastrpc/hqc/dsp/hqc_dsp.c`

Changed purpose:

- Add the actual DSP worker-pool backend.
- Keep existing `direct`, `copy`, and `l2fetch` paths unchanged.
- Use the SDK default worker pool instead of creating another pool per call.
- Reduce worker-branch stack pressure so HQC-256 can run.

New mode:

```c
#define HQC_BUFFER_MODE_WORKER_POOL 4
```

Initial HQC-256 failure:

- HQC-256 worker mode failed on device with `FastRPC error=0x8000040d`.
- The failure reproduced even with `iters=1 codeword_count=1`.
- Since `codeword_count=1` submits no worker jobs, the failure was not caused
  by worker thread decode itself.
- The retained fix removes the extra `worker_pool_init()`/`worker_pool_deinit()`
  per call and removes unnecessary worker-branch stack buffers.

New job struct:

```c
typedef struct {
    const unsigned char *codewords;
    unsigned char *messages;
    int codeword_stride;
    int message_stride;
    int start;
    int end;
    int decodes;
    int ok;
    uint32_t checksum;
    worker_synctoken_t *token;
} hqc_worker_decode_job_t;
```

Old serial flow:

```c
for (int iter = 0; iter < iters; ++iter) {
    for (int idx = 0; idx < codeword_count; ++idx) {
        hqc_decode_direct(...);
    }
}
```

New worker flow:

```c
for (int iter = 0; iter < iters; ++iter) {
    worker_pool_synctoken_init(&token, submitted);

    for (unsigned int r = 1; r < ranges; ++r) {
        worker_pool_submit(context, worker_job);
    }

    hqc_decode_worker_range(&jobs[0]);
    worker_pool_synctoken_wait(&token);
}
```

`context` is `0`, so `worker_pool_submit()` uses the SDK default static worker
pool created by `libworker_pool` at skel load time.

Worker range behavior:

```c
for (int idx = job->start; idx < job->end; ++idx) {
    const uint8_t *src = job->codewords + (size_t)idx * job->codeword_stride;
    uint8_t *dst = job->messages + (size_t)idx * job->message_stride;

    hqc_decode_direct(dst, (const uint64_t *)src);
}
```

Dispatch behavior:

```c
if (dsp_mode == HQC_BUFFER_MODE_WORKER_POOL) {
#if HQC_USE_WORKER_POOL
    return hqc_decode_buffer_bench_worker(...);
#else
    *mode_status = HQC_BUFFER_STATUS_UNSUPPORTED;
    return 0;
#endif
}
```

Net behavior:

- `direct`, `copy`, and `l2fetch` still use the old single-thread path.
- `worker` splits the batch into independent ranges.
- Range 0 runs on the parent DSP thread.
- Other ranges run on worker-pool threads.
- Each range writes to a separate output slice.
- The host sees one FastRPC call either way.
- Serial `direct`/`copy`/`l2fetch` scratch buffers now live in
  `hqc_decode_buffer_bench_serial()`, so the worker branch does not carry the
  large `local_codeword` frame used only by serial copy mode.

## Worker Setup

Environment:

- Device is accessed from WSL through Windows adb:
  `/mnt/c/Temp/ADB/platform-tools/adb.exe`
- FastRPC deploy path used for worker tests:
  `/data/local/tmp/QDC_files/hqc_worker_trial_<level>`
- DSP arch used:
  `HEXAGON_ARCH=v73`
- Project source path:
  `labs/fastest`

### 1. Build-Time Worker Pool Setup

Worker pool is optional. Normal builds still use `HQC_USE_WORKER_POOL=0`.
For this trial it is enabled with:

```sh
HQC_USE_WORKER_POOL=1
```

The build script resolves the SDK worker-pool files from:

```sh
WORKER_POOL_ROOT="$HEXAGON_SDK_ROOT/libs/worker_pool"
WORKER_POOL_INC_DIR="$WORKER_POOL_ROOT/inc"
WORKER_POOL_LIB="$WORKER_POOL_ROOT/prebuilt/hexagon_toolv19_v68/libworker_pool.a"
```

Then it adds:

```sh
-I "$WORKER_POOL_INC_DIR"
-DHQC_USE_WORKER_POOL=1
"$WORKER_POOL_LIB"
```

So the DSP skel gets:

- `worker_pool.h`
- `libworker_pool.a`
- `HQC_USE_WORKER_POOL=1`

### 2. Important Runtime Strategy

The retained strategy uses the SDK default static worker pool.

`libworker_pool` creates this default pool in its constructor when the DSP skel
is loaded. In `hqc_decode_buffer_bench_worker()`, `context` is left as `0`:

```c
worker_pool_context_t context = 0;
worker_pool_submit(context, worker_job);
```

Passing `context = 0` makes `worker_pool_submit()` use the default static pool.

Do not create another pool inside every benchmark call:

```c
worker_pool_init(&context);   // not used in retained strategy
worker_pool_deinit(&context); // not used in retained strategy
```

That earlier approach failed for HQC-256 with `FastRPC error=0x8000040d`.
The fix is:

- use the SDK default pool;
- do not init/deinit a second pool per call;
- keep serial-path stack buffers out of the worker branch;
- let each worker decode directly into its own output slice.

### 3. Batch Split Setup

The worker mode chooses how many ranges to create from SDK runtime values:

```c
unsigned int ranges = num_hvx128_contexts;
if (ranges == 0 || ranges > num_workers) {
    ranges = num_workers;
}
if (ranges > MAX_NUM_WORKERS) {
    ranges = MAX_NUM_WORKERS;
}
if (ranges > (unsigned int)codeword_count) {
    ranges = (unsigned int)codeword_count;
}
```

For every iteration:

- range 0 runs on the parent DSP thread;
- ranges 1..N are submitted with `worker_pool_submit()`;
- a `worker_synctoken_t` waits for submitted ranges;
- each range writes to disjoint `messages[idx * message_stride]`.

Worker range decode shape:

```c
const uint8_t *src = codewords + (size_t)idx * codeword_stride;
uint8_t *dst = messages + (size_t)idx * message_stride;
hqc_decode_direct(dst, (const uint64_t *)src);
```

### 4. Build Command

Build command shape:

```sh
env ADB=/mnt/c/Temp/ADB/platform-tools/adb.exe \
    HQC_USE_WORKER_POOL=1 \
    HQC_PARAM_LEVEL=<128|192|256> \
    HEXAGON_ARCH=v73 \
    HQC_PROJECT_DIR=/home/hiiamming/Code/test/hexagon-tutorial/hqc/labs/fastest \
    bash fastrpc/hqc/build_android_gcc_bionic.sh
```

### 5. Deploy Command

Deploy command shape:

```sh
/mnt/c/Temp/ADB/platform-tools/adb.exe shell mkdir -p /data/local/tmp/QDC_files/hqc_worker_trial_<level>
/mnt/c/Temp/ADB/platform-tools/adb.exe push \
    fastrpc/hqc/build/hqc_host \
    fastrpc/hqc/build/libhqc_skel.so \
    fastrpc/hqc/build/testsig-0xaa3ec42e.so \
    /data/local/tmp/QDC_files/hqc_worker_trial_<level>/
```

### 6. Device Environment

Device environment:

```sh
cd /data/local/tmp/QDC_files/hqc_worker_trial_<level>
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
```

## How To Run

### Simulator VLIW/Stall Trial

Run after applying the `labs/fastest` RS/RM changes:

```sh
HQC1_BENCH_ITERS=1 bash labs/fastest/scripts/run_hqc1_decode_bench_hexagon.sh
HQC1_BENCH_ITERS=10 bash labs/fastest/scripts/run_hqc1_decode_bench_hexagon.sh
HQC3_BENCH_ITERS=10 bash labs/fastest/scripts/run_hqc3_decode_bench_hexagon.sh
HQC5_BENCH_ITERS=10 bash labs/fastest/scripts/run_hqc5_decode_bench_hexagon.sh
```

### FastRPC Worker-Pool Device Trial

Run direct and worker with the same harness:

```sh
./hqc_host buffer-bench rpcmem-cached direct 100 16
./hqc_host buffer-bench rpcmem-cached worker 100 16
```

For HQC-128 batch-size scaling:

```sh
./hqc_host buffer-bench rpcmem-cached direct 100 64
./hqc_host buffer-bench rpcmem-cached worker 100 64
```

Do not compare these `buffer-bench` numbers directly with the older whole
`NPU fastest non-CT` rows. The older whole rows mostly measure fixture decode
already inside DSP. `buffer-bench` additionally measures reading codewords from
a host-visible shared buffer and writing messages back.

## Results

### Simulator Baseline Before VLIW/Stall Changes

| HQC | Iters | Result | Total decodes | Total insns | Total Pcycles |
| --- | ---: | --- | ---: | ---: | ---: |
| HQC-128 | 1 | PASS | 16 | 3,193,459 | 4,237,215 |
| HQC-128 | 10 | PASS | 160 | 7,935,714 | 10,517,886 |
| HQC-192 | 10 | PASS | 160 | 10,548,560 | 14,365,359 |
| HQC-256 | 10 | PASS | 160 | 15,677,361 | 21,124,572 |

HQC-128 hot estimate:

```text
(10,517,886 - 4,237,215) / (9 * 16) = 43,615.77 Pcycles/decode
```

### Simulator After VLIW/Stall Changes

| HQC | Iters | Result | Total decodes | Total insns | Total Pcycles | Baseline Pcycles | Delta |
| --- | ---: | --- | ---: | ---: | ---: | ---: | ---: |
| HQC-128 | 1 | PASS | 16 | 3,189,899 | 4,201,407 | 4,237,215 | -0.85% |
| HQC-128 | 10 | PASS | 160 | 7,907,458 | 10,174,062 | 10,517,886 | -3.27% |
| HQC-192 | 10 | PASS | 160 | 10,556,326 | 13,971,003 | 14,365,359 | -2.75% |
| HQC-256 | 10 | PASS | 160 | 15,693,421 | 20,496,792 | 21,124,572 | -2.97% |

HQC-128 hot estimate:

```text
(10,174,062 - 4,201,407) / (9 * 16) = 41,476.77 Pcycles/decode
```

### Device Worker-Pool Results

Same harness comparison: `buffer-bench rpcmem-cached`.


HQC-256 fix note:

```text
Before the stack/pool fix:
./hqc_host buffer-bench rpcmem-cached worker 1 1
FastRPC error=0x8000040d

After the stack/pool fix:
./hqc_host buffer-bench rpcmem-cached worker 1 1
result=PASS

./hqc_host buffer-bench rpcmem-cached worker 100 16
us_per_decode=33.978 result=PASS
```

### Worker Batch-Size Sweep

Command shape:

```sh
for c in 8 16 32 64 128 256 512 1024; do
    ./hqc_host buffer-bench rpcmem-cached worker 100 $c
done
```

HQC-128 also tested `2048`.

| HQC | codeword_count=8 | 16 | 32 | 64 | 128 | 256 | 512 | 1024 | 2048 | Best |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| HQC-128 | 11.869 | 19.152 | 17.055 | 12.454 | 11.676 | 10.734 | 11.113 | 11.270 | 12.079 | 256 / 10.734 |
| HQC-192 | 18.167 | 25.233 | 22.835 | 18.058 | 16.576 | 17.330 | 17.463 | 18.233 | n/a | 128 / 16.576 |
| HQC-256 | 25.080 | 34.001 | 31.593 | 24.393 | 24.362 | 24.824 | 26.082 | 29.527 | n/a | 128 / 24.362 |

Interpretation:

- Larger batch is useful only until worker/FastRPC overhead is amortized.
- After that, larger shared input/output buffers start hurting cache/bandwidth.
- Best measured values on this device:
  - HQC-128: `codeword_count=256`
  - HQC-192: `codeword_count=128`
  - HQC-256: `codeword_count=128`

## Current Decision

- Keep the VLIW/stall simulator changes because all tested levels improved.
- Keep worker mode for HQC-128, HQC-192, and HQC-256 because same-harness
  device results improved by `2.25x` to `3.01x`.
