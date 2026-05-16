# HQC-128 concatenated codec scalar baseline

This folder contains the portable scalar C reference encode/decode path copied from
`git/hqc_gitlab`, keeping the same source split:

- `src/common/code.c`: concatenated wrapper, RS encode then RM encode, and RM decode then RS decode.
- `src/common/reed_muller.h` plus `src/ref/reed_muller.c`: duplicated RM(1,7).
- `src/ref/hqc-1/reed_solomon.h` plus `src/ref/reed_solomon.c`: HQC-128 Reed-Solomon.
- `src/ref/hqc-1/parameters.h`: minimal HQC-128 codec parameters only.
- `src/ref/gf.*` and `src/common/fft.*`: dependencies for RS decode.

Run the scalar baseline on Hexagon simulator:

```sh
bash scripts/run_hqc128_codec_hexagon.sh
```

The script builds `demos/hqc128_codec_demo.c` with `hexagon-clang` without
`-mhvx`, then runs it through `hexagon-sim` + H2 `booter`. It expects the tutorial tools layout under
`../tools`, or these environment variables:

```sh
export HEXAGON_TUTORIAL_ROOT=/path/to/hexagon-tutorial
export HEXAGON_SDK_ROOT=$HEXAGON_TUTORIAL_ROOT/tools/hexagon-sdk
```

## Decode benchmark fixture

The decode benchmark keeps encode out of the measured Hexagon binary. Generate
the corrupt HQC-128 codeword fixture on the host:

```sh
bash scripts/gen_hqc128_decode_fixture.sh
```

Then run the host decode-only benchmark:

```sh
bash scripts/run_hqc128_decode_bench_host.sh
```

Or build and run the same decode-only scalar benchmark on Hexagon:

```sh
bash scripts/run_hqc128_decode_bench_hexagon.sh
```

`demos/hqc128_decode_bench.c` links the generated fixture plus decode
dependencies only: `fft.c`, `gf.c`, `reed_muller.c`, and `reed_solomon.c`.
It intentionally does not link `src/common/code.c`. The benchmark scripts also
compile with function/data sections and linker garbage collection, so unused
RS/RM encode functions from the shared source files are dropped from the final
benchmark binary.
