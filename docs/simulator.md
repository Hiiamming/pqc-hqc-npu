# Simulator Benchmarks

Run full decode on the Hexagon simulator:

```sh
scripts/sim_decode.sh --variant scalar --level 128 --bench decode --iters 10
scripts/sim_decode.sh --variant fastest --level 128 --bench decode --iters 10
scripts/sim_decode.sh --variant ct --level 256 --bench decode --iters 10
```

Run stage and substage measurements:

```sh
scripts/sim_decode.sh --variant fastest --level 128 --bench stage --stage 1 --iters 10
scripts/sim_decode.sh --variant ct --level 192 --bench substage --substage 5 --iters 10
```

The wrapper forwards to the original per-lab scripts, so existing stage and substage benchmark code is preserved.
