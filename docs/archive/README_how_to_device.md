# HQC FastRPC + cDSP/HVX Device Bring-Up Guide

This file is the operational checklist for taking the HQC decoder from the
Hexagon simulator to a real Qualcomm device through FastRPC + cDSP. The current
`hqc_fastrpc_intrinsic/` app is an HQC-128 FastRPC wrapper; extend the same
pattern for HQC-192/HQC-256 after the HQC-128 device path is stable.

## 0. Baseline Assumptions

- Host machine builds the ARM64 host binary and the Hexagon cDSP skel library.
- Device runs Linux or Android on a Qualcomm SoC with cDSP FastRPC available.
- Runtime model is one host process calling one cDSP method that performs the
  whole benchmark loop on DSP. Do not call FastRPC once per decode when
  benchmarking; RPC overhead will dominate.
- The expected output for a healthy run includes `result=PASS`.

Important repo paths:

```sh
hqc_fastrpc_intrinsic/      # FastRPC cDSP HVX/intrinsic path
hqc_fastrpc_scalar/         # FastRPC cDSP scalar baseline
scalar_on_board_cpu/        # ARM64 CPU-only scalar baseline
README_dev_formalize.md     # Existing board results and environment notes
```

## 1. Check Host Build Environment

Run these on the build machine:

```sh
pwd
command -v aarch64-linux-gnu-gcc
command -v qaic || true
ls -l ../tools/hexagon-sdk/ipc/fastrpc/qaic/Ubuntu/qaic
ls -l ../tools/hexagon-sdk/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-clang
ls -l ../tools/hexagon-sdk/ipc/fastrpc/remote/ship/UbuntuARM_aarch64/libcdsprpc.so
ls -l ../tools/hexagon-sdk/tools/elfsigner/output/testsig-0xaa3ec42e.so
```

If `aarch64-linux-gnu-gcc` is missing on Ubuntu/Debian:

```sh
sudo apt-get update
sudo apt-get install gcc-aarch64-linux-gnu
```

If `qaic`, `hexagon-clang`, `libcdsprpc.so`, or `testsig-0xaa3ec42e.so` is
missing, the Hexagon SDK/tool install is incomplete or `HEXAGON_SDK_ROOT` points
at the wrong directory. Fix the SDK path before touching the device:

```sh
export HEXAGON_SDK_ROOT=/absolute/path/to/hexagon-sdk
bash hqc_fastrpc_intrinsic/build.sh
```

## 2. Check Device Capability: Linux

For the RB3 Gen 2/QCS6490 style setup used in `README_dev_formalize.md`, connect
through SSH first:

```sh
ssh -p 2222 -i qdc_id_2026-5-13_317.pem root@localhost
```

Then check the board:

```sh
uname -a
cat /sys/devices/soc0/serial_number 2>/dev/null || true
cat /sys/devices/soc0/soc_id 2>/dev/null || true
cat /sys/devices/soc0/machine 2>/dev/null || true
cat /sys/devices/soc0/family 2>/dev/null || true
ls -l /dev/*dsp* /dev/adsprpc* /dev/fastrpc* 2>/dev/null || true
ls -l /usr/lib/dsp/cdsp /dsp/cdsp 2>/dev/null || true
ls -l /usr/lib/libcdsprpc.so* 2>/dev/null || true
```

What you need:

- Some FastRPC/adsprpc device node under `/dev`.
- A cDSP RFSA/search path such as `/usr/lib/dsp/cdsp` or `/dsp/cdsp`.
- A host-side `libcdsprpc.so`.

If `/dev/*dsp*`, `/dev/adsprpc*`, or `/dev/fastrpc*` is missing, the device
kernel/userspace is not exposing FastRPC. Use a kernel/firmware image with
FastRPC/cDSP enabled, check boot logs, or switch to a board image known to
support cDSP. Useful debug commands:

```sh
dmesg | grep -i -E 'fastrpc|adsprpc|cdsp|remoteproc|subsys' | tail -n 100
ps -ef | grep -i -E 'cdsp|fastrpc|adsprpc' || true
```

If `/usr/lib/dsp/cdsp` is missing, the DSP library search path may be different
on that image. QCS8550/Kalama Linux images commonly expose `/dsp/cdsp`. Find it:

```sh
find / -type d -path '*dsp*' 2>/dev/null | head -n 50
find / -name 'libcdsprpc.so*' -o -name '*cdsp*' 2>/dev/null | head -n 50
```

If the search shows `/dsp/cdsp`, use that path in `ADSP_LIBRARY_PATH` when
running the FastRPC binary.

## 3. Check Device Capability: Android

For the QRD8650/Android setup in `README_dev_formalize.md`, connect through adb:

```sh
adb devices
adb root || true
adb shell
```

Run:

```sh
uname -a
getprop ro.product.board
getprop ro.board.platform
getprop ro.soc.model
getprop ro.soc.manufacturer
cat /sys/devices/soc0/serial_number 2>/dev/null || true
cat /sys/devices/soc0/soc_id 2>/dev/null || true
ls -l /dev/*dsp* /dev/adsprpc* /dev/fastrpc* 2>/dev/null || true
ls -l /vendor/lib/rfsa/adsp /vendor/lib/rfsa/cdsp /dsp 2>/dev/null || true
ls -l /vendor/lib64/libcdsprpc.so* /system/lib64/libcdsprpc.so* 2>/dev/null || true
getenforce 2>/dev/null || true
```

What you need:

- FastRPC/adsprpc device node exists and is accessible to your shell/user.
- DSP RFSA directories exist, commonly `/vendor/lib/rfsa/cdsp`,
  `/vendor/lib/rfsa/adsp`, or `/dsp`.
- `libcdsprpc.so` is available in `/vendor/lib64` or another system path.

If device nodes exist but open/load fails, check SELinux and permissions:

```sh
id
ls -Z /dev/*dsp* /dev/adsprpc* /dev/fastrpc* 2>/dev/null || true
getenforce
```

On a development device only, this can help distinguish policy from code bugs:

```sh
setenforce 0
```

If that fixes FastRPC, the long-term fix is SELinux policy or running under an
allowed context, not leaving production devices permissive.

## 4. Build the Device Artifacts

Build the cDSP intrinsic FastRPC path:

```sh
bash hqc_fastrpc_intrinsic/build.sh
```

For Android targets, build the host with an Android NDK compiler and link
against an Android `libcdsprpc.so`, not the default Ubuntu ARM64 FastRPC library.
The wrapper below follows the QRD8650 Android setup in `README_dev_formalize.md`:
it uses a Bionic PIE host, RUNPATH
`/apex/com.android.runtime/lib64/bionic:/system/lib64:/vendor/lib64`, and checks
for interpreter `/system/bin/linker64`.

```sh
ANDROID_NDK_HOME=/path/to/android-ndk \
ANDROID_API=34 \
bash hqc_fastrpc_intrinsic/build_android.sh
```

If the Android NDK is not available, use the GCC/Bionic workaround. This is the
path used to reproduce the HDK8550 Android run from this workspace. It pulls the
Android runtime libraries from the attached device, builds the DSP skel with the
normal Hexagon toolchain, then overwrites `hqc_fastrpc_intrinsic/build/hqc_host`
with a Bionic PIE FastRPC host using interpreter `/system/bin/linker64`.

```sh
HEXAGON_ARCH=v68 \
HQC_PARAM_LEVEL=128 \
bash hqc_fastrpc_intrinsic/build_android_gcc_bionic.sh
```

This workaround still runs the decoder on cDSP. The ARM64 `hqc_host` only opens
the cDSP FastRPC module and invokes `libhqc_skel.so`; it does not contain a CPU
decode fallback. A missing `libhqc_skel.so` should make `hqc_open` fail instead
of silently running locally.

For the cDSP scalar FastRPC baseline:

```sh
ANDROID_NDK_HOME=/path/to/android-ndk \
ANDROID_API=34 \
bash hqc_fastrpc_scalar/build_android.sh
```

To build, deploy, smoke-test, and run the HQC-128 Android cDSP intrinsic
benchmark in one step:

```sh
ANDROID_NDK_HOME=/path/to/android-ndk \
ANDROID_API=34 \
bash bench_android_hqc1.sh
```

When using the GCC/Bionic workaround, build first and then let the same wrapper
only deploy and run:

```sh
HEXAGON_ARCH=v68 HQC_PARAM_LEVEL=128 \
bash hqc_fastrpc_intrinsic/build_android_gcc_bionic.sh

SKIP_BUILD=1 \
DEVICE_DIR=/data/local/tmp/QDC_files/hdk8550_fastrpc_intrinsic_pass12_fastest \
SMOKE_ITERS=10 \
BENCH_ITERS=10000 \
bash bench_android_hqc1.sh
```

By default this builds HQC-128. The same FastRPC wrapper can build one
parameter set at a time:

```sh
HQC_PARAM_LEVEL=128 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=192 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=256 bash hqc_fastrpc_intrinsic/build.sh
```

Each build overwrites `hqc_fastrpc_intrinsic/build/hqc_host` and
`hqc_fastrpc_intrinsic/build/libhqc_skel.so`, so rebuild the intended mode
immediately before copying artifacts to the device.

For an older cDSP target, keep the default `HEXAGON_ARCH=v68`. If the target and
SDK support a newer Hexagon architecture, override it:

```sh
HEXAGON_ARCH=v73 bash hqc_fastrpc_intrinsic/build.sh
```

Expected outputs:

```sh
ls -l hqc_fastrpc_intrinsic/build/hqc_host
ls -l hqc_fastrpc_intrinsic/build/libhqc_skel.so
ls -l hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so
file hqc_fastrpc_intrinsic/build/hqc_host
file hqc_fastrpc_intrinsic/build/libhqc_skel.so
readelf -l hqc_fastrpc_intrinsic/build/hqc_host | grep interpreter
```

For Android, the interpreter must be `/system/bin/linker64`. If it is
`/lib/ld-linux-aarch64.so.1`, the host is a Linux/glibc binary and should not be
used on Android.

Also build baselines when comparing real device performance:

```sh
bash hqc_fastrpc_scalar/build.sh
bash scalar_on_board_cpu/build_arm64.sh
```

If the build fails with missing `remote.h`, `AEEStdDef.h`, or FastRPC headers,
`HEXAGON_SDK_ROOT` is wrong or the SDK install is incomplete. If `-lcdsprpc`
cannot be found, check:

```sh
ls -l "$HEXAGON_SDK_ROOT/ipc/fastrpc/remote/ship/UbuntuARM_aarch64"
```

## 5. Verify the DSP Library Was Built for HVX/HMX

On the host:

```sh
../tools/hexagon-sdk/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-llvm-readelf \
  -A hqc_fastrpc_intrinsic/build/libhqc_skel.so
```

Expected for an HVX build:

```text
hvx_arch = ...
```

If HMX is enabled in the build/target, also expect:

```text
hmx_arch = ...
```

Check actual vector instructions:

```sh
../tools/hexagon-sdk/tools/HEXAGON_Tools/19.0.04/Tools/bin/hexagon-llvm-objdump \
  -d hqc_fastrpc_intrinsic/build/libhqc_skel.so \
  | rg -m 30 'vmem|vdeal|vadd|vsub|vabs|vmax|vmin|vxor'
```

If no HVX instructions appear, you probably built the wrong source path, missed
`-mhvx -mhvx-length=128B`, or compiled the scalar FastRPC folder instead.

## 6. Deploy to a Linux Device

Create the target directory:

```sh
ssh -p 2222 -i qdc_id_2026-5-13_317.pem root@localhost \
  "mkdir -p /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic"
```

Copy artifacts:

```sh
scp -P 2222 -i qdc_id_2026-5-13_317.pem \
  hqc_fastrpc_intrinsic/build/hqc_host \
  hqc_fastrpc_intrinsic/build/libhqc_skel.so \
  hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so \
  root@localhost:/data/local/tmp/QDC_files/hqc_fastrpc_intrinsic/
```

Run:

```sh
ssh -p 2222 -i qdc_id_2026-5-13_317.pem root@localhost
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp;/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 1000
```

For the main benchmark:

```sh
./hqc_host 10000
```

If `ldd ./hqc_host` says `libcdsprpc.so` is not found, fix `LD_LIBRARY_PATH` or
copy/install the correct ARM64 FastRPC runtime library for that board.

## 7. Deploy to an Android Device

The one-step wrapper is the preferred path after either Android build method:

```sh
SKIP_BUILD=1 \
DEVICE_DIR=/data/local/tmp/QDC_files/hdk8550_fastrpc_intrinsic_pass12_fastest \
SMOKE_ITERS=10 \
BENCH_ITERS=10000 \
bash bench_android_hqc1.sh
```

Push artifacts:

```sh
adb shell "mkdir -p /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic"
adb push hqc_fastrpc_intrinsic/build/hqc_host /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic/
adb push hqc_fastrpc_intrinsic/build/libhqc_skel.so /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic/
adb push hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic/
```

Run:

```sh
adb shell
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 1000
```

For the main benchmark:

```sh
./hqc_host 10000
```

If the host binary does not start on Android, inspect it:

```sh
file ./hqc_host
readelf -l ./hqc_host | grep 'interpreter' || true
```

Android normally wants a Bionic PIE executable with interpreter
`/system/bin/linker64`. A glibc-linked Linux binary may fail on Android unless
it was built in a way compatible with that environment.

For the HDK8550 Android fastest intrinsic run, the runtime folder used in the
benchmark notes is:

```sh
/data/local/tmp/QDC_files/hdk8550_fastrpc_intrinsic_pass12_fastest
```

Use `HEXAGON_ARCH=v68` when reproducing the old HDK8550 numbers.

## 8. Interpret FastRPC Errors

The host prints hints for common FastRPC errors.

Common cases:

- `hqc_open failed`: cDSP domain open failed, FastRPC device is missing,
  inaccessible, or domain selection is wrong.
- `FastRPC error=0x80000406`: `libhqc_skel.so` was not found by the DSP loader.
  Fix `ADSP_LIBRARY_PATH` and confirm the skel is in the current directory.
- `FastRPC error=0x80000403`: signature/load rejection. Copy
  `testsig-0xaa3ec42e.so`, enable unsigned module in development, or use the
  correct signing flow for the device.
- Process starts but hangs/crashes: check `dmesg`, `logcat`, and whether the
  DSP image supports the Hexagon arch used at build time.

Linux debug:

```sh
dmesg | grep -i -E 'fastrpc|adsprpc|cdsp|remoteproc|subsys|hqc' | tail -n 100
```

Android debug:

```sh
logcat -d | grep -i -E 'fastrpc|adsprpc|cdsp|remoteproc|hqc' | tail -n 200
dmesg | grep -i -E 'fastrpc|adsprpc|cdsp|remoteproc|subsys|hqc' | tail -n 100
```

## 9. Confirm the Benchmark Is Measuring DSP Work

A good run looks like:

```text
[fastrpc-intrinsic-decode] calling cDSP HVX intrinsic decoder, iters=10000
[fastrpc-intrinsic-decode] total_decodes=160000 ... result=PASS
[fastrpc-intrinsic-decode] elapsed_ms=... us_per_decode=...
```

The `total_decodes` should be:

```text
iters * 16 fixtures
```

Run a small call and a large call:

```sh
./hqc_host 1
./hqc_host 1000
./hqc_host 10000
```

If `us_per_decode` drops a lot from `iters=1` to `iters=1000`, that is normal:
the fixed FastRPC open/load/call cost is being amortized. Use the large run for
performance comparisons.

To rule out a local CPU fallback on Android, run a negative no-skel test in a
separate directory:

```sh
adb shell "mkdir -p /data/local/tmp/QDC_files/hdk8550_noskel_test"
adb push hqc_fastrpc_intrinsic/build/hqc_host /data/local/tmp/QDC_files/hdk8550_noskel_test/
adb shell '
cd /data/local/tmp/QDC_files/hdk8550_noskel_test
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD"
export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"
./hqc_host 1
'
```

Expected result:

```text
[fastrpc-intrinsic-decode] hqc_open failed, error=0x80000406
```

That failure means the FastRPC host needs the DSP skel and did not fall back to
a CPU decoder.

## 10. Compare Against Baselines

Deploy and run the cDSP scalar baseline:

```sh
mkdir -p /data/local/tmp/QDC_files/hqc_fastrpc_scalar
# copy hqc_fastrpc_scalar/build/hqc_host, libhqc_skel.so, testsig-0xaa3ec42e.so
cd /data/local/tmp/QDC_files/hqc_fastrpc_scalar
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host 10000
```

Deploy and run the ARM64 CPU baseline:

```sh
mkdir -p /data/local/tmp/QDC_files/scalar_on_board_cpu
# copy scalar_on_board_cpu/build/hqc1_decode_bench_arm64
cd /data/local/tmp/QDC_files/scalar_on_board_cpu
chmod +x hqc1_decode_bench_arm64
./hqc1_decode_bench_arm64
```

Record at least:

```text
variant
iters
total_decodes
result
elapsed_ms
us_per_decode
```

Use `README_dev_formalize.md` as the format for final board-result tables.

## 11. Optional Energy Measurement

On Linux boards exposing battery/current sysfs, use:

```sh
./measure_board_energy.sh LABEL ./hqc_host 10000
```

If the voltage/current paths differ, override them:

```sh
VOLTAGE_PATH=/sys/class/power_supply/.../voltage_now \
CURRENT_PATH=/sys/class/power_supply/.../current_now \
SAMPLE_INTERVAL=0.1 \
./measure_board_energy.sh hqc_intrinsic ./hqc_host 10000
```

If the sysfs files do not exist, the script cannot measure energy on that
device without a different sensor path or external power measurement.

## 12. HQC-192/HQC-256 FastRPC

The simulator intrinsic code and the FastRPC wrapper support HQC-128,
HQC-192, and HQC-256. First check the simulator path for the same parameter set:

```sh
bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh
bash hqc_lab_insintric/scripts/run_hqc3_decode_bench_hexagon.sh
bash hqc_lab_insintric/scripts/run_hqc5_decode_bench_hexagon.sh
```

Then build exactly one FastRPC parameter set and deploy the generated artifacts:

```sh
HQC_PARAM_LEVEL=128 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=192 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=256 bash hqc_fastrpc_intrinsic/build.sh
```

The fixture headers intentionally use `PARAM_K` for message size:

- HQC-128: `PARAM_K=16`
- HQC-192: `PARAM_K=24`
- HQC-256: `PARAM_K=32`

So seeing 16 bytes for HQC-128 messages is expected. Do not hand-edit fixtures
unless changing test vectors; regenerate missing fixtures with the matching
`hqc_lab_insintric/scripts/gen_hqc*_decode_fixture.sh` script.

After deploying a mode, run `./hqc_host 1` first, then `./hqc_host 1000` or a
larger benchmark only after `result=PASS`.

## 13. Final Acceptance Checklist

Before trusting a device result:

- Simulator full decode passes for the same parameter set.
- DSP library has `hvx_arch` in `readelf -A`.
- DSP disassembly contains HVX ops such as `vdeal`, `vadd`, `vsub`, `vmax`,
  `vmin`, or `vmem`.
- Device exposes FastRPC/cDSP nodes and libraries.
- `./hqc_host 1` returns `result=PASS`.
- `./hqc_host 1000` or `./hqc_host 10000` returns `result=PASS`.
- `total_decodes == iters * 16`.
- The benchmark uses one FastRPC call for the whole loop, not one call per
  decode.
- CPU scalar and cDSP scalar baselines were measured on the same device if you
  are reporting speedups.
