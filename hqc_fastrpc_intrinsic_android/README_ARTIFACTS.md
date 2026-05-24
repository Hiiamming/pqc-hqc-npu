# HQC Android Prebuilt Artifacts

These folders hold Android-ready artifacts for HDK8550 / Android U runs.
They are built once on the host and can be pushed directly to the device
without recompiling.

## CPU Scalar Artifacts

CPU scalar binaries live under:

```text
scalar_on_board_cpu/android_artifacts/hqc128_cpu_scalar/
scalar_on_board_cpu/android_artifacts/hqc192_cpu_scalar/
scalar_on_board_cpu/android_artifacts/hqc256_cpu_scalar/
```

Each folder contains one Android/Bionic PIE executable:

```text
hqc128_decode_bench_arm64_android
hqc192_decode_bench_arm64_android
hqc256_decode_bench_arm64_android
```

Deploy and run example:

```sh
ADB="/mnt/c/Temp/ADB/platform-tools/adb.exe"
level=192
src="scalar_on_board_cpu/android_artifacts/hqc${level}_cpu_scalar"
dst="/data/local/tmp/QDC_files/hqc_prebuilt/hqc${level}_cpu_scalar"

"$ADB" shell "mkdir -p '$dst'"
"$ADB" push "$src/." "$dst/"
"$ADB" shell "cd '$dst' && chmod +x hqc${level}_decode_bench_arm64_android && ./hqc${level}_decode_bench_arm64_android"
```

## NPU Fastest Non-CT Artifacts

FastRPC cDSP/HVX fastest artifacts live under:

```text
hqc_fastrpc_intrinsic_android/hqc128_npu_fastest_nonct/
hqc_fastrpc_intrinsic_android/hqc192_npu_fastest_nonct/
hqc_fastrpc_intrinsic_android/hqc256_npu_fastest_nonct/
```

Each folder contains:

```text
hqc_host
libhqc_skel.so
testsig-0xaa3ec42e.so
```

Deploy and run example:

```sh
ADB="/mnt/c/Temp/ADB/platform-tools/adb.exe"
level=192
src="hqc_fastrpc_intrinsic_android/hqc${level}_npu_fastest_nonct"
dst="/data/local/tmp/QDC_files/hqc_prebuilt/hqc${level}_npu_fastest_nonct"

"$ADB" shell "mkdir -p '$dst'"
"$ADB" push "$src/." "$dst/"
"$ADB" shell "cd '$dst' && chmod +x hqc_host && \
  export ADSP_LIBRARY_PATH=\"\$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp\" && \
  export LD_LIBRARY_PATH=\"\$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic\" && \
  ./hqc_host 10000"
```

## Build Settings

CPU scalar artifacts were built with:

```text
HQC_BENCH_ITERS=10000
```

NPU artifacts were built with:

```text
HEXAGON_ARCH=v68
HQC_DEFAULT_BENCH_ITERS=10000
HQC_RS_FAST_NON_CT=1
HQC_GF_LUT_MUL=1
HQC_RM_EXPAND_LUT=1
HQC_RM_FUSED_FAST=1
HQC_RS_ROOTS_HVX=1
```

The ARM64 host binaries are Android/Bionic PIE executables with interpreter:

```text
/system/bin/linker64
```
