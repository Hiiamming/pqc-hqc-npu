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

Useful overrides:

```sh
ADB=/path/to/adb scripts/measure_android.sh \
  --suite paper \
  --levels "128" \
  --repeats 1 \
  --target-decodes 32000
```

The paper suite writes raw summaries plus aggregate CSV and generated TeX rows.
The direct path uses `scripts/measure_board_energy.sh` on-device. qprof remains
diagnostic-only because it can perturb power and clock state; run
`scripts/measure_qprof.sh` separately when cDSP utilization or clock context is
needed.

The suite implementation is internal under `scripts/lib/`. CT and worker
experiments are archived under `scripts/archive/` and intentionally excluded
from this paper workflow.
