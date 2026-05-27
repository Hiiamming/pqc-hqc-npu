# Android QDC + Qualcomm Profiler Setup

This note records the working setup for the QDC Android cloud device
`HDK8550 / Snapdragon 8 Gen 2 / Android U` and how to run DSP/NPU profiling.

Use three shells deliberately:

- WSL shell: repo work, scripts, and Windows `.exe` through `/mnt/c/...`.
- Windows PowerShell: SSH tunnel management when needed.
- Android shell: commands after `adb shell`.

The current repo root is:

```sh
cd /home/hiiamming/Code/test/hexagon-tutorial/hqc
```

## 1. Host Paths

Known working paths on this machine:

```text
Windows ADB:
C:\Temp\ADB\platform-tools\adb.exe

Qualcomm Profiler CLI:
C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows-64\bins\qprof.exe

Qualcomm Profiler target files:
C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la

Qualcomm Profiler metric DB:
C:\Program Files (x86)\Qualcomm\Shared\Prof_Ext\ExtQProfiler.db
```

From WSL, the same executables are:

```sh
ADB_WIN="/mnt/c/Temp/ADB/platform-tools/adb.exe"
QPROF_HOST="/mnt/c/Program Files (x86)/Qualcomm/QualcommProfiler/CLI/host-windows-64/bins/qprof.exe"
```

Prefer the Windows `adb.exe` for this QDC session. The Linux `adb` inside WSL
can start its own server, but it does not see the cloud device in this setup.

## 2. Check ADB Device

Run from WSL:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" devices -l
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell getprop ro.product.board
```

Expected device:

```text
e5a06b3e device product:kalama model:Kalama_for_arm64 device:kalama
kalama
```

If there is no device, check the QDC SSH tunnel that forwards ADB. On this
session it looked like:

```text
ssh -i secrets/qdc_id_2026-5-13_317.pem -L 5037:sa606759.sa.svc.cluster.local:5037 -N sshtunnel@ssh.qdc.qualcomm.com
```

The `sa606759...` host can change between QDC sessions. Read the active SSH
process if needed:

```powershell
Get-CimInstance Win32_Process |
  Where-Object { $_.CommandLine -like "*ssh.qdc.qualcomm.com*" } |
  Select-Object ProcessId,CommandLine | Format-List
```

## 3. Prepare Android for Profiler Files

Run from WSL:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" root
"/mnt/c/Temp/ADB/platform-tools/adb.exe" remount
```

Expected remount output includes:

```text
Verity is already disabled
Remounted /vendor as RW
Remount succeeded
```

If `/vendor` is still read-only, `InstallerLA.exe` will fail while pushing
`/vendor/bin/qprof` or `/vendor/qprof/...`. Re-run `adb remount` after the
device finishes rebooting.

## 4. Install Qualcomm Profiler Target Files

The official installer can work after `/vendor` is RW:

```sh
"/mnt/c/Program Files (x86)/Qualcomm/Shared/QualcommProfiler/API/target-la/InstallerLA.exe" -s -d e5a06b3e
```

The `-s` flag does simple installation only. It pushes target files but does not
try to start the server on a WLAN IP. This matters for the cloud device because
the Android side only has loopback exposed.

If the installer is too slow because it validates every file with `adb pull`,
bulk-push the same files manually from PowerShell:

```powershell
& 'C:\Temp\ADB\platform-tools\adb.exe' shell mkdir -p /vendor/bin /vendor/qprof/libs /vendor/qprof/backends /data/shared/qcom/Shared/Prof_Ext

& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la\aarch64\bins\.' /vendor/bin/

& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la\aarch64\libs\.' /vendor/qprof/libs/

& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la\aarch64\libs\backends\.' /vendor/qprof/backends/

& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\Prof_Ext\ExtQProfiler.db' /data/shared/qcom/Shared/Prof_Ext/

& 'C:\Temp\ADB\platform-tools\adb.exe' shell chmod -R 755 /vendor/bin/qprof /vendor/bin/qmonitor-grpc-server /vendor/bin/profilerUtilityApp /vendor/qprof
```

Final target layout:

```text
/vendor/bin/qprof
/vendor/bin/qmonitor-grpc-server
/vendor/bin/profilerUtilityApp
/vendor/qprof/libs
/vendor/qprof/backends/libQMonitorProfilerBackend.so
/data/shared/qcom/Shared/Prof_Ext/ExtQProfiler.db
```

## 5. Required Android Shell Environment

Every new Android shell that runs `qprof` needs:

```sh
export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs
```

Quick check:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --help'
```

## 6. Start Profiler Server

For on-device profiling commands, `qprof` can run directly and does not need the
host `qprof.exe`. If a server is needed, start it from `/data/local/tmp` instead
of `/vendor/bin`; on this Android U image `/vendor/bin/qmonitor-grpc-server`
can hit linker namespace issues with `libandroid.so`.

One-time copy to `/data/local/tmp`:

```powershell
& 'C:\Temp\ADB\platform-tools\adb.exe' shell mkdir -p /data/local/tmp/qprof/bins /data/local/tmp/qprof/libs
& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la\aarch64\bins\.' /data/local/tmp/qprof/bins/
& 'C:\Temp\ADB\platform-tools\adb.exe' push 'C:\Program Files (x86)\Qualcomm\Shared\QualcommProfiler\API\target-la\aarch64\libs\.' /data/local/tmp/qprof/libs/
& 'C:\Temp\ADB\platform-tools\adb.exe' shell chmod -R 755 /data/local/tmp/qprof
```

Start server:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export LD_LIBRARY_PATH=/vendor/qprof/libs:/data/local/tmp/qprof/libs:$LD_LIBRARY_PATH; export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; nohup /data/local/tmp/qprof/bins/qmonitor-grpc-server -l -p 62472 -n 127.0.0.1 -vv >/data/local/tmp/qmonitor.log 2>&1 &'
```

Check server:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'ps -A | grep qmonitor; ss -ltnp | grep 62472 || true; tail -80 /data/local/tmp/qmonitor.log'
```

Expected listener:

```text
qmonitor-grpc-server
LISTEN [::ffff:127.0.0.1]:62472
```

## 7. Check Available Profilers

Run directly on the device through ADB:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --capabilities'
```

Working capabilities seen on this device include:

```text
profiler:apps-proc-cpu-metrics
profiler:proc-gpu-specific-metrics
profiler:adsp-dsp-metrics
profiler:nsp-dsp-metrics
profiler:nsp1-dsp-metrics
profiler:adsp-dsp-stats
profiler:nsp-dsp-stats
profiler:nsp1-dsp-stats
```

`profiler:cdsp-dsp-metrics` was not the right name on this Android U Kalama
image. Use `adsp`, `nsp`, and `nsp1` capability names from `qprof --capabilities`.

## 8. Run Measurement Commands

ADSP stats, 1 second:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --profile --profile-type async --file-format json --capabilities-list profiler:adsp-dsp-stats --profile-time 1 --streaming-rate 1000 --live --result-format verbose'
```

NPU0 stats:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --profile --profile-type async --file-format json --capabilities-list profiler:nsp-dsp-stats --profile-time 1 --streaming-rate 1000 --live --result-format verbose'
```

NPU1 stats:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --profile --profile-type async --file-format json --capabilities-list profiler:nsp1-dsp-stats --profile-time 1 --streaming-rate 1000 --live --result-format verbose'
```

ADSP metrics with selected metric IDs:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --profile --profile-type async --file-format json --capabilities-list profiler:adsp-dsp-metrics --profile-time 10 --streaming-rate 200 --live --result-format verbose --metric-id-list 4096 4097 4099 4182'
```

Results are written on the device under:

```text
/data/shared/QualcommProfiler/profilingresults/<timestamp>/
```

Pull a result directory:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" pull /data/shared/QualcommProfiler/profilingresults ./qprof_results
```

## 9. Measuring While Running HQC

Use two terminals.

Terminal 1 starts a longer profiler window:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends; export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/vendor/qprof/libs; qprof --profile --profile-type async --file-format json --capabilities-list profiler:adsp-dsp-stats --profile-time 30 --streaming-rate 1000 --live --result-format verbose'
```

Terminal 2 runs the workload during that window. For the Android FastRPC build,
build from WSL:

```sh
cd /home/hiiamming/Code/test/hexagon-tutorial/hqc
bash hqc_fastrpc_intrinsic/build_android.sh
```

Upload to the device:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'mkdir -p /data/local/tmp/hqc_fastrpc_intrinsic'
"/mnt/c/Temp/ADB/platform-tools/adb.exe" push hqc_fastrpc_intrinsic/build/hqc_host hqc_fastrpc_intrinsic/build/libhqc_skel.so /data/local/tmp/hqc_fastrpc_intrinsic/
```

Run on the device:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'cd /data/local/tmp/hqc_fastrpc_intrinsic; chmod +x hqc_host; export LD_LIBRARY_PATH=$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic; export ADSP_LIBRARY_PATH="$PWD;/vendor/dsp/cdsp;/vendor/lib/rfsa/adsp;/vendor/lib64/rfsa/adsp"; ./hqc_host 1000'
```

Expected HQC output should include `result=PASS`. If FastRPC cannot find the DSP
library, inspect DSP/RFSA paths:

```sh
"/mnt/c/Temp/ADB/platform-tools/adb.exe" shell 'find /vendor -iname "*cdsp*" -o -path "*rfsa*" 2>/dev/null | head -50'
```

## 10. Whole HQC CPU vs NPU Measurement

For the current comparison, measure HQC-128, HQC-192, and HQC-256 on:

- CPU: scalar ARM64 baseline only, built as an Android/Bionic PIE executable
  for Android devices.
- NPU: current fastest non-CT FastRPC path only.

Run from the repo root:

```sh
cd /home/hiiamming/Code/test/hexagon-tutorial/hqc
scripts/measure_android_decode.sh
```

The script builds each level, pushes binaries to:

```text
/data/local/tmp/QDC_files/hqc_whole/
```

It runs an idle qprof window before each CPU/NPU workload, then runs qprof while
the workload executes. Raw qprof logs and pulled device profiler result folders
are saved under:

```text
qprof_hqc_whole_runs/
```

Default benchmark iterations are `10000` for every CPU and NPU level. Keep that
unless you only need a smoke test; shorter NPU-192/NPU-256 runs can finish before
qprof captures enough DSP metric samples.

The final comparison table is written to:

```text
README_result_whole.md
```

Useful overrides:

```sh
PROFILE_TIME=60 scripts/measure_android_decode.sh
CPU_ITERS_128=20000 CPU_ITERS_192=2000 CPU_ITERS_256=1000 scripts/measure_android_decode.sh
NPU_ITERS_128=20000 NPU_ITERS_192=2000 NPU_ITERS_256=1000 scripts/measure_android_decode.sh
LEVELS="128" scripts/measure_android_decode.sh
```

The NPU build is forced to the fastest non-CT configuration:

```text
HQC_RS_FAST_NON_CT=1
HQC_GF_LUT_MUL=1
HQC_RM_EXPAND_LUT=1
HQC_RM_FUSED_FAST=1
HQC_RS_ROOTS_HVX=1
```

For a single ad-hoc measurement around an already deployed workload, use:

```sh
PROFILE_TIME=30 IDLE_POWER_W=0.70 scripts/measure_qprof.sh npu1 hqc1_npu \
  'cd /data/local/tmp/QDC_files/hqc_whole/hqc1_npu_fastest_nonct && ./hqc_host 10000'
```

## 11. Host qprof.exe Caveat

The host CLI can be configured:

```sh
"/mnt/c/Program Files (x86)/Qualcomm/QualcommProfiler/CLI/host-windows-64/bins/qprof.exe" --configure --server-ip 127.0.0.1 --port 62472
```

However, for this QDC cloud topology the host `qprof.exe --capabilities` was not
stable even after opening an extra Windows SSH tunnel for port `62472`. The
reliable path is:

```text
WSL -> Windows adb.exe -> adb shell -> device qprof
```

Use direct `adb shell qprof ...` commands for measurements unless the QDC
network topology is changed to expose the profiler gRPC port cleanly to Windows.

## 12. Common Problems

`adb devices` in WSL shows nothing:

- Use `C:\Temp\ADB\platform-tools\adb.exe`, not `/usr/bin/adb`.
- Check the QDC SSH tunnel that forwards `5037`.

`InstallerLA.exe` fails with read-only `/vendor`:

- Run `adb root`, then `adb remount`.
- Wait for any reboot to finish, then run installer or bulk-push again.

`qmonitor-grpc-server` reports `libandroid.so not found` from `/vendor/bin`:

- Start the copy under `/data/local/tmp/qprof/bins/qmonitor-grpc-server`.
- Keep `LD_LIBRARY_PATH=/vendor/qprof/libs:/data/local/tmp/qprof/libs:$LD_LIBRARY_PATH`.

`qprof --capabilities` works but `cdsp-dsp-*` does not exist:

- This device exposes `adsp-dsp-*`, `nsp-dsp-*`, and `nsp1-dsp-*`.
- Always trust `qprof --capabilities` for the exact capability names.
