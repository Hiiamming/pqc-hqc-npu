# HQC Decoder Repo Layout

This repo is now organized around the only paths that are still used for HQC decoder measurement:

- `labs/scalar`: portable scalar baseline and CPU decode benchmark sources.
- `labs/fastest`: Hexagon HVX/HMX fastest non-CT decoder path.
- `labs/ct`: Hexagon HVX/HMX constant-time decoder path.
- `fastrpc/hqc`: shared FastRPC host/skel wrapper for `fastest` and `ct`.
- `runners/scalar_cpu`: ARM64 CPU benchmark builders for Linux and Android.
- `scripts`: simulator, Linux, Android, direct-energy, and qprof measurement entrypoints.

For the current Android real-device workflow, use
`scripts/measure_android.sh --suite paper`. This measures both the ARM64 CPU
scalar baseline and the FastRPC cDSP fastest path. Run
`scripts/measure_android.sh --suite boundary` separately for FastRPC boundary
overhead.

The stage and substage simulator benchmarks are intentionally kept under each lab's `scripts/` and `demos/` directories. Use `scripts/sim_decode.sh --bench stage` or `--bench substage` as the stable wrapper.

The repo root no longer keeps shell entrypoint wrappers. Run scripts from `scripts/`.

`README.md` is left untouched for now and may still mention old paths.
