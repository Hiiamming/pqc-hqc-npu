# Linux Device Measurement

CPU scalar:

```sh
scripts/measure_linux_decode.sh --backend cpu --level 128 --iters 10000
```

FastRPC fastest or CT:

```sh
scripts/measure_linux_decode.sh --backend fastest --level 192 --iters 10000
scripts/measure_linux_decode.sh --backend ct --level 256 --iters 10000
```

FastRPC Linux runs assume the Qualcomm FastRPC runtime and device nodes are already available on the target. Override SDK/toolchain paths with the existing `HEXAGON_SDK_ROOT`, `QAIC`, `HEXAGON_CLANG`, `AARCH64_CC`, and `FASTRPC_LIB_DIR` variables.
