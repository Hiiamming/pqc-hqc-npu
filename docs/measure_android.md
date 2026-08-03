# Android Device Measurement

Use the stable entrypoint for the real-device paper measurements:

```sh
ADB=/path/to/adb scripts/measure_android.sh --suite paper
```

This measures the ARM64 scalar baseline and the `labs/fastest` FastRPC cDSP
path. The defaults match the methodology used by `references/Benchmark.tex`:

- `HEXAGON_ARCH=v73`
- HQC levels `128 192 256`
- `5` repeats
- `32000` decodes per workload
- direct board-energy sampling with qprof disabled
- process CPU sampling from `/proc/<pid>/task/*/stat`

Run the FastRPC boundary suite separately:

```sh
ADB=/path/to/adb scripts/measure_android.sh --suite boundary
```

The boundary suite records open/close, ping, payload, decode-one, and batched
decode measurements. Its defaults match the current `result.md` rerun:
`5` repeats, `10000` ping/payload/decode-one calls, `100` open/close calls, and
`125` batched benchmark iterations.

Run the batch-size latency sweep separately:

```sh
ADB=/path/to/adb scripts/measure_android.sh --suite batch
```

The batch suite runs the existing FastRPC `buffer-bench` mode with
`rpcmem-cached` host buffers and `direct` DSP access. It sweeps batch sizes
`1 2 4 8 16 32 64 128 256` by default and records real-device
`us_per_decode` for each point. This isolates how increasing the number of
codewords per FastRPC call amortizes the fixed FastRPC boundary cost. Each
measured point uses one DSP iteration per RPC and repeats RPC calls until it
reaches the target decode count.

Run the paper-style batch-size sweep when comparing directly with the paper
batched latency table:

```sh
ADB=/path/to/adb scripts/measure_android.sh --suite paper-batch
```

This suite keeps the fixture corpus compiled into the DSP binary and measures
one FastRPC call per point. The batch size is the number of fixture decodes
inside that single call, so the curve shows how the fixed FastRPC cost is
amortized without host-buffer transfer cost.

Useful overrides:

```sh
ADB=/path/to/adb scripts/measure_android.sh \
  --suite paper \
  --levels "128" \
  --repeats 1 \
  --target-decodes 32000
```

For a quick batch smoke test:

```sh
ADB=/path/to/adb scripts/measure_android.sh \
  --suite batch \
  --levels "128" \
  --repeats 1 \
  --batch-sizes "1 16" \
  --target-decodes 512
```

For a quick paper-style batch smoke test:

```sh
ADB=/path/to/adb scripts/measure_android.sh \
  --suite paper-batch \
  --levels "128" \
  --repeats 1 \
  --batch-sizes "1 256 32768"
```

The paper suite writes raw summaries plus aggregate CSV and generated TeX rows.
The direct path uses `scripts/measure_board_energy.sh` on-device. qprof remains
diagnostic-only because it can perturb power and clock state; run
`scripts/measure_qprof.sh` separately when cDSP utilization or clock context is
needed.

The suite implementation is internal under `scripts/lib/`. CT and worker
experiments are archived under `scripts/archive/` and intentionally excluded
from this paper workflow.
