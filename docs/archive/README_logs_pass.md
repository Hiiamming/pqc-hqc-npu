# HQC-{128,192,256} Hexagon Lab Notes

## Current layout
sysdump meminfo check DMA up better
Qualcomm Profiler
• Được, dùng CLI được. Kết quả NPU:

  - NPU0 có xuất hiện: profiler:nsp-dsp-metrics
  - NPU1 có xuất hiện: profiler:nsp1-dsp-metrics
  - NPU1 profile chạy được và có số đo thật.
  - NPU0 hiện query metrics fail: sysmon profiler query metrics failed with error code= -2

  Lệnh check NPU:

  "C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows-64\bins\qprof.exe" --capabilities

  Lệnh profile NPU1 chạy được:

  "C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows-64\bins\qprof.exe" --profile --capabilities-list
  profiler:nsp1-dsp-metrics --profile-time 3 --streaming-rate 1000 --sampling-rate 200 --live --result-format verbose
  --summary

  Sample NPU1 đo được:

  MPPS avg: 0.742914
  QDSP6 Load avg: 2.320946 MCPS
  QDSP6 Utilization avg: 0.156990 %
  QDSP Clock: 1478.400 MHz
  MemNoc Clock Vote: 1012.320 MHz

  File kết quả nằm ở:

  C:\ProgramData\Qualcomm\QualcommProfiler\profilingresults\22-05-2026_12-23-54\

  Nếu server chết sau reboot/disconnect, start lại bằng:

  adb shell 'nohup sh -c "export QMONITOR_BACKEND_LIB_PATH=/vendor/qprof/backends/; export LD_LIBRARY_PATH=\
  $LD_LIBRARY_PATH:/vendor/qprof/libs/; /vendor/bin/qprof --configure --server-ip 192.168.5.82 --port 62472; /vendor/
  bin/qprof --start-server" >/data/local/tmp/qprof-server.log 2>&1 &'

  Tóm lại: bỏ GUI được. NPU1 đo được bằng CLI; NPU0 có expose capability nhưng hiện không query được metrics trên device
  này.


   Có. CLI hỗ trợ realtime bằng flag --live.

  Ví dụ xem realtime NPU1:

  "C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows-64\bins\qprof.exe" --profile --capabilities-list
  profiler:nsp1-dsp-metrics --profile-time 30 --streaming-rate 1000 --sampling-rate 200 --live --result-format verbose

  - --profile-time 30: chạy 30 giây
  - --streaming-rate 1000: in dữ liệu mỗi 1 giây
  - --sampling-rate 200: sample mỗi 200 ms
  - --live: stream ra terminal realtime

  Muốn realtime gọn hơn, chỉ xem vài metric quan trọng:

  "C:\Program Files (x86)\Qualcomm\QualcommProfiler\CLI\host-windows-64\bins\qprof.exe" --profile --capabilities-list
  profiler:nsp1-dsp-metrics --profile-time 30 --streaming-rate 1000 --sampling-rate 200 --metric-id-list 4096 4097 4098
  4182 --live --result-format verbose

  Metric chính:

  4096 = MPPS
  4097 = QDSP6 Load
  4098 = QDSP6 Utilization
  4182 = QDSP Clock

  Nếu muốn gần như “monitor” liên tục, tăng --profile-time, ví dụ 3600 cho 1 giờ.

• Có đo được, nhưng cần phân biệt:

  - Profiler đo được battery voltage/current, CPU load/clock, thermal, NPU metrics.
  - Nó không đo trực tiếp “NPU rail power” riêng. Energy tính được từ battery là whole-device power, phù hợp để so CPU
    vs NPU nếu benchmark được kiểm soát tốt.

  Trên device này có battery metrics:

  4703 Battery Voltage      micro V
  4705 Battery Current      micro amperes
  4706 Battery Capacity     %
  4708 Battery Temperature  deg C

  Công thức xấp xỉ:

  Power W = voltage_uV * abs(current_uA) / 1e12
  Energy J = sum(Power W * dt seconds)

  Để so HQC CPU vs NPU, mình sẽ đo kiểu này:

  CPU run:

  qprof.exe --profile ^
    --capabilities-list profiler:apps-proc-cpu-metrics profiler:apps-proc-battery-metrics profiler:apps-proc-thermal-
  metrics ^
    --metric-id-list 4616 4696 4703 4705 4708 6464 ^
    --profile-time 30 --streaming-rate 1000 --sampling-rate 200 ^
    --live --result-format csv --summary

  NPU run:

  qprof.exe --profile ^
    --capabilities-list profiler:nsp1-dsp-metrics profiler:apps-proc-battery-metrics profiler:apps-proc-thermal-metrics
  ^
    --metric-id-list 4096 4097 4098 4182 4183 4703 4705 4708 6464 ^
    --profile-time 30 --streaming-rate 1000 --sampling-rate 200 ^
    --live --result-format csv --summary

  NPU metrics quan trọng:
  4097 QDSP6 Load
  4098 QDSP6 Utilization
  4182 QDSP Clock
  4183 MemNoc Clock Vote

  Cách benchmark HQC hợp lý:

  1. Chạy mỗi backend nhiều vòng cố định, ví dụ 1000 decaps/decode.
  2. Đo wall time riêng bằng app: ns/op, ops/s.
  3. Đồng thời log Profiler battery current/voltage.
  4. Tính:
      - energy_per_decode = total_energy_J / num_decodes
      - throughput_per_watt = decodes_per_second / avg_power_W
  5. So thêm CPU load/clock và thermal để biết có throttling không.

  Lưu ý: nếu device đang cắm sạc, Battery Current có thể bị nhiễu hoặc đổi dấu. Muốn số energy nghiêm túc nhất thì chạy
  bằng pin, màn hình cố định brightness, tắt background app, warm-up trước rồi đo.

────────────────────────────────────────────────────────────────────────────────────────────────────────────────────────


› nma cái đo năng lượng đấy là như nào z, đang đo trên toàn mạch à


• Đúng, kiểu đo bằng Battery Voltage + Battery Current là đo ở mức toàn thiết bị, gần như công suất vào/ra từ pin, không
  phải riêng NPU.

  Nó phản ánh tổng tiêu thụ của cả board/device tại thời điểm đó:

  CPU + NPU/DSP + GPU + DRAM + display + Wi-Fi + storage + sensors + PMIC loss + background services

  Công thức:

  Power ≈ Voltage × Current

  Với Profiler:

  Power_W = BatteryVoltage_microV * abs(BatteryCurrent_microA) / 1e12

  Ví dụ:

  Voltage = 3,850,000 uV
  Current = -800,000 uA
  Power ≈ 3.08 W

  Sau đó tích phân theo thời gian:

  Energy_J = Σ Power_W * Δt

  Nhưng đây là whole-device energy, nên khi so CPU vs NPU phải đo kiểu delta:

  Idle baseline power:   P_idle
  CPU run avg power:     P_cpu_total
  NPU run avg power:     P_npu_total

  CPU extra power ≈ P_cpu_total - P_idle
  NPU extra power ≈ P_npu_total - P_idle

  Rồi:

  CPU energy/decode ≈ (P_cpu_total - P_idle) * runtime / num_decodes
  NPU energy/decode ≈ (P_npu_total - P_idle) * runtime / num_decodes

  Nó vẫn không phải “NPU rail power”, nhưng đủ dùng để trả lời câu thực dụng: port lên NPU có làm toàn thiết bị tốn ít
  năng lượng hơn / nhanh hơn không.

  Muốn đo riêng rail NPU/SoC chuẩn hơn thì cần một trong các thứ này:

  - power rail monitor exposed bởi board/kernel
  - PMIC fuel gauge/rail debugfs/sysfs nếu có
  - Trepn/Power Monitor tương thích platform
  - external power analyzer đo nguồn cấp board
  - Qualcomm internal tools nếu device hỗ trợ

  Với setup hiện tại, mình sẽ dùng: idle baseline + battery current/voltage + runtime + NPU/CPU utilization/clock.

- `hqc_lab_scalar/`: portable scalar C baseline. Its Hexagon decode benchmark intentionally builds without `-mhvx`.
- `hqc_lab_insintric/`: Hexagon HVX intrinsic variant. The folder name keeps the spelling requested in the task.
- Each lab has three parameter-set folders under `src/ref/`: `hqc-1/` (= HQC-128), `hqc-3/`, `hqc-5/`. Build scripts pick the right one via `-I src/ref/hqc-<set>`, so `parameters.h` and the RS `alpha_ij_pow` precomputed table resolve to the active variant. The shared sources in `src/ref/` (`gf.c`, `reed_muller.c`, `reed_solomon.c`) and `src/common/` (`fft.c`, `code.c`) are written against `PARAM_N1`, `PARAM_N2`, `PARAM_DELTA`, `PARAM_K`, `PARAM_G`, `PARAM_FFT`, `PARAM_M` and so are reused unchanged for all three security levels.
- Each lab generates an independent 16-fixture corpus per set: `fixtures/hqc{128,192,256}_decode_fixture.c`, produced by `tools/gen_hqc{128,192,256}_decode_fixture.c` via the matching `scripts/gen_hqc{128,192,256}_decode_fixture.sh`. Run scripts auto-invoke the generator if the `.c` is missing.

## Run commands

HQC-128 (default):

  `HQC1_BENCH_ITERS=100 bash hqc_lab_scalar/scripts/run_hqc1_decode_bench_hexagon.sh`
  `HQC1_BENCH_ITERS=100 bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh`

HQC-192 and HQC-256 use the same script naming with the prefix swapped, and the matching `HQC3_BENCH_ITERS` / `HQC5_BENCH_ITERS` variable:

  `HQC3_BENCH_ITERS=10 bash hqc_lab_insintric/scripts/run_hqc3_decode_bench_hexagon.sh`
  `HQC5_BENCH_ITERS=10 bash hqc_lab_insintric/scripts/run_hqc5_decode_bench_hexagon.sh`

## Parameter sets (from the HQC spec, 2025-08-22)

| Set | PARAM_N1 | PARAM_N2 | PARAM_N1N2 | PARAM_DELTA | PARAM_K | PARAM_G | PARAM_FFT | RM multiplicity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| HQC-128 | 46 | 384 | 17,664 | 15 | 16 | 31 | 4 | 3 |
| HQC-192 | 56 | 640 | 35,840 | 16 | 24 | 33 | 5 | 5 |
| HQC-256 | 90 | 640 | 57,600 | 29 | 32 | 59 | 5 | 5 |

The RS field is the same GF(2^8) with primitive polynomial `0x11D` for every set. The per-set Reed-Solomon generator polynomial coefficients are encoded in `RS_POLY_COEFS` in each `parameters.h`. The `alpha_ij_pow[2*PARAM_DELTA][PARAM_N1-1]` table is precomputed and embedded in `src/ref/hqc-<set>/reed_solomon.h`.

## Plan A: multiplicity-5 RM expand LUT

Status: simulator-validated and QRD8650 hardware-validated for direct latency.

Reason for the change:

- HQC-128 uses RM multiplicity 3 and already had a dedicated `rm_expand3_nibble_table[4096]` path.
- HQC-192 and HQC-256 use RM multiplicity 5 and previously fell back to generic arithmetic expansion.
- The generic multiplicity-5 `rm_expand` path was the strongest suspect for HQC-192 scaling badly on QRD8650.

Patch strategy:

- Do not build a direct 5-copy table (`16^5` entries), because that is too large.
- Reuse the existing 3-copy table and add a small 2-copy table:

  ```text
  5-copy expand = lookup(copy0, copy1, copy2) + lookup(copy3, copy4)
  ```

- Added `rm_expand2_nibble_table[256]`, about 2 KiB, and enabled the 3+2 LUT path only for `MULTIPLICITY == 5`.

Simulator check, `iters=100`, all cases PASS:

| Check | Before Pcycles | After Pcycles | Improvement |
| --- | ---: | ---: | ---: |
| HQC-128 `rm_expand` | 27,393,015 | 27,393,015 | unchanged |
| HQC-192 `rm_expand` | 216,966,471 | 58,957,980 | 3.68x |
| HQC-256 `rm_expand` | 346,715,262 | 92,668,596 | 3.74x |
| HQC-128 full decode | 81,134,130 | 81,134,130 | unchanged |
| HQC-192 full decode | 275,067,750 | 120,965,505 | 2.27x |
| HQC-256 full decode | 440,345,550 | 192,572,535 | 2.29x |

Pcycles per decode after Plan A, with 1600 decodes per run:

| HQC | `rm_expand` Pcycles/decode | Full decode Pcycles/decode |
| --- | ---: | ---: |
| 128 | 17,120.6 | 50,708.8 |
| 192 | 36,848.7 | 75,603.4 |
| 256 | 57,917.9 | 120,357.8 |

Remaining loopholes before calling the strategy hardware-proven:

- The simulator can overstate or understate table-cache behavior versus QRD8650 cDSP.
- The Android FastRPC path can include runtime/cache/setup effects not present in simulator full decode.
- QRD8650 qprof energy/thermal metrics are invalid in the current session, so validation should use direct wall-clock first and qprof counters second.

Local Android Plan A artifacts are ready here:

```text
hqc_fastrpc_intrinsic_android/hqc3_npu_fastest_nonct_planA/
hqc_fastrpc_intrinsic_android/hqc5_npu_fastest_nonct_planA/
```

QRD8650 hardware validation on device `ac6088de`:

- Energy/thermal sanity failed again: qprof idle reported about `0.026 W` and
  thermal max `4294694.296000 C`, so energy and thermal are not valid for this
  QRD8650 session.
- Direct Plan A latency is therefore the primary result. Each line below is
  one `./hqc_host 10000` run, i.e. `160000` decodes.

| HQC | Old NPU direct us/decode | Plan A direct us/decode | Speedup vs old NPU | Old CPU scalar us/decode | Plan A speedup vs CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| 192 | 106.550 / 106.508 / 106.737 | 41.218 / 41.210 / 41.208 | about 2.59x | 92.266 / 92.278 / 92.342 | about 2.24x |
| 256 | 165.896 / 165.898 / 165.916 | 62.753 / 62.756 / 62.759 | about 2.64x | 202.058 / 202.030 / 203.999 | about 3.22x |

qprof NPU-only runs were useful for DSP counters but perturb the wall-clock
latency. After qprof, direct runs stayed at the slower qprof-clock state until
reboot. Treat direct post-reboot latency as the timing result and qprof as
counters only:

| HQC | qprof run us/decode | NPU util avg % | QDSP clk MHz | HMX util avg % | Note |
| --- | ---: | ---: | ---: | ---: | --- |
| 192 | 60.456 | 99.035969 | 1252.417391 | 0.000000 | `profile_rc=139`, workload PASS |
| 256 | 92.225 | 99.112840 | 1251.952026 | 0.000000 | rerun; `profile_rc=139`, workload PASS |

Raw qprof logs are under:

```text
qprof_qrd8650_planA_runs/
```

HDK8550 hardware validation on device `417a1107`:

- Existing HQC-128 HDK8550 numbers were kept as the reference because the
  previous energy/counter run was already valid.
- HQC-192 and HQC-256 were rerun on the NPU/cDSP FastRPC path after Plan A.
- Energy was measured with qprof battery voltage/current and an immediately
  preceding idle baseline. For Plan A, shorter qprof windows were used where
  possible because the optimized workload finishes much faster than the older
  30 s qprof window.

Clean direct post-reboot latency:

| HQC | Old NPU us/decode | Plan A direct us/decode | Speedup vs old NPU | Old CPU scalar us/decode | Plan A speedup vs CPU |
| --- | ---: | ---: | ---: | ---: | ---: |
| 192 | 113.198 | 50.779 / 50.748 / 50.743 | about 2.23x | 103.392 | about 2.04x |
| 256 | 176.205 | 77.079 / 77.065 / 77.109 | about 2.29x | 227.038 | about 2.95x |

qprof energy/counter runs:

| HQC | qprof run | us/decode | delta W | uJ/decode | NPU util avg % | QDSP clk MHz | HMX util avg % | Note |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 192 | tight energy/timing | 50.748 | 0.208192631 | 10.565 |  |  |  | Energy/timing clean; NPU counters blank. |
| 192 | counter run | 63.886 | 1.230158302 | 78.590 | 98.897193 | 1165.874385 | 0.000000 | Counters valid; qprof perturbed timing/power. |
| 256 | tight energy/counter | 97.075 | 2.501786568 | 242.860 | 99.686675 | 1172.041121 | 0.000000 | Energy and counters valid, but qprof latency is slower than direct. |

Raw qprof logs are under:

```text
qprof_hdk8550_planA_runs/
```

Measurement caveat:

- `BOARD_RESULTS.md` used direct sysfs battery sampling through
  `measure_board_energy.sh`, while the HDK8550 table in `README_result_whole.md`
  uses qprof battery metrics. These are not the same harness.
- qprof can perturb cDSP/NPU clock state; after qprof, direct runs stayed at
  qprof-like latency until reboot. Use post-reboot direct latency for speed and
  qprof rows for battery/counter evidence.
- HMX stayed at `0`, so this remains cDSP/QDSP-HVX through FastRPC, not HMX
  matrix-engine offload.

Deploy and hardware validation commands:

```sh
for level in 192 256; do
  src="hqc_fastrpc_intrinsic_android/hqc${level}_npu_fastest_nonct_planA"
  dst="/data/local/tmp/QDC_files/hqc_whole/hqc${level}_npu_fastest_nonct_planA"
  /mnt/c/Temp/ADB/platform-tools/adb.exe shell "mkdir -p '$dst'"
  /mnt/c/Temp/ADB/platform-tools/adb.exe push "$src/hqc_host" "$dst/"
  /mnt/c/Temp/ADB/platform-tools/adb.exe push "$src/libhqc_skel.so" "$dst/"
  /mnt/c/Temp/ADB/platform-tools/adb.exe push "$src/testsig-0xaa3ec42e.so" "$dst/"
done

/mnt/c/Temp/ADB/platform-tools/adb.exe shell 'set -e; for level in 192 256; do cd /data/local/tmp/QDC_files/hqc_whole/hqc${level}_npu_fastest_nonct_planA; export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp"; export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic"; for i in 1 2 3; do ./hqc_host 10000; done; done'
```

## Default fastest intrinsic comparison across HQC-128/192/256

These runs use the current `hqc_lab_insintric` default fastest Hexagon simulator path. The old fastest selector flags are no longer needed; the scripts compile directly with HVX/HMX enabled and the decode path selected by the active parameter-set include directory.

Commands:

  `HQC1_BENCH_ITERS=10 bash hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh`
  `HQC3_BENCH_ITERS=10 bash hqc_lab_insintric/scripts/run_hqc3_decode_bench_hexagon.sh`
  `HQC5_BENCH_ITERS=10 bash hqc_lab_insintric/scripts/run_hqc5_decode_bench_hexagon.sh`

All three parameter sets used the 16-fixture corpus. Estimated Pcycles/decode uses `(10-iter Pcycles - 1-iter Pcycles) / (9 * 16)`.

| Set | 1-iter result | 1-iter Pcycles | 10-iter result | 10-iter Pcycles | Estimated Pcycles/decode | Relative to HQC-128 |
| --- | --- | ---: | --- | ---: | ---: | ---: |
| HQC-128 | PASS | 4,426,449 | PASS | 12,865,581 | 58,605 | 1.00x |
| HQC-192 | PASS | 6,164,940 | PASS | 31,104,852 | 173,194 | 2.96x |
| HQC-256 | PASS | 8,047,161 | PASS | 48,302,166 | 279,549 | 4.77x |

## HQC-192 / HQC-256 compatibility with the HQC-128 optimization passes

The shared intrinsic source files inherit the full optimization stack (passes 1-12 below), but a few opt-in fast paths were originally written assuming HQC-128's specific parameters. The HQC-192/256 extension preserves all HQC-128 perf gains and adds explicit guards for the remaining mismatches:

- `expand_and_sum_hvx` in `hqc_lab_insintric/src/ref/reed_muller.c` keeps the HQC-128 LUT and packed-arithmetic paths under `#if MULTIPLICITY == 3`, and falls back to a `MULTIPLICITY`-agnostic packed-halfword sum loop for HQC-192/256 (which both use `MULTIPLICITY = 5`). The fallback is selected automatically by the include path; no flag change is needed.
- `HQC_RM_EXPAND_LUT` and `HQC_RM_FUSED_FAST` are HQC-128-only opt-ins; the file rejects them at compile time for any other `MULTIPLICITY`.
- `HQC_RS_ROOTS_HVX` packs all `PARAM_N1` Chien-search support positions across one or more 64-lane HVX vectors. HQC-128 (46 positions) and HQC-192 (56) use a single vector; HQC-256 (90) uses two. The vector count is `RS_SUPPORT_VEC_COUNT = CEIL_DIVIDE(PARAM_N1, 64)`, computed at compile time, and a `#error` rejects any hypothetical future PARAM_N1 > 128.

Default HQC-192 and HQC-256 builds (no opt-in flags) compile cleanly and decode correctly. The verified non-`#error` opt-ins so far are:

| Flag | HQC-128 | HQC-192 | HQC-256 |
| --- | :-: | :-: | :-: |
| `HQC_USE_HVX_RS_SYNDROME` (default on) | ✅ | ✅ | ✅ |
| `HQC_USE_GF_HWSTYLE_MUL` (default on) | ✅ | ✅ | ✅ |
| `HQC_USE_GF_LUT_MUL` | ✅ | ✅ | ✅ |
| `HQC_RM_EXPAND_LUT` | ✅ | `#error` | `#error` |
| `HQC_RM_FUSED_FAST` | ✅ | `#error` | `#error` |
| `HQC_RS_ROOTS_HVX` | ✅ | ✅ | ✅ (2-vector path) |

## Implementation summary

| Pass | Brief change | Main file / section |
| --- | --- | --- |
| 1 | Split scalar baseline from HVX intrinsic build; added HVX RM Hadamard add/sub and peak absolute-max reduction. | `hqc_lab_scalar/src/ref/reed_muller.c` baseline; `hqc_lab_insintric/src/ref/reed_muller.c`: `hadamard_hvx`, `find_peaks_hvx`, `reed_muller_decode` |
| 2 | Removed scalar even/odd gather from HVX Hadamard by using HVX deal/deinterleave. | `hqc_lab_insintric/src/ref/reed_muller.c`: `hadamard_hvx` |
| 3 | Replaced scalar peak scan with HVX predicate/index-vector tie-break logic. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_index_lo`, `rm_index_hi`, `find_peaks_hvx` |
| 4 | Added HVX RM expand/sum path and RM-vs-RS stage benchmark. | `hqc_lab_insintric/src/ref/reed_muller.c`: `expand_and_sum_hvx`; `hqc_lab_insintric/demos/hqc1_decode_stage_bench.c`; `hqc_lab_insintric/scripts/run_hqc1_decode_stage_bench_hexagon.sh` |
| 5 | Added fixed-flow hardware-style GF multiplier and opt-in GF LUT experiment. | `hqc_lab_insintric/src/ref/gf.c`: `gf_mul_hwstyle`, `gf_mul`; intrinsic benchmark scripts: `HQC_USE_GF_HWSTYLE_MUL`, `HQC_GF_LUT_MUL` |
| 6 | Vectorized RS syndrome computation with HVX and defaulted it in intrinsic Hexagon scripts. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_syndromes_hvx`; `hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh`: `HQC_HVX_RS_SYNDROME` |
| 7 | Targeted top substage bottlenecks: packed RM expand/sum, shortened RS Chien roots, and gated fast non-CT RS ELP/error-values experiments. | `hqc_lab_insintric/src/ref/reed_muller.c`: `expand_and_sum_hvx`; `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_elp`, `compute_roots`, `compute_error_values`; `hqc_lab_insintric/demos/hqc1_decode_substage_bench.c` |
| 8 | Optimized the fastest benchmark-only RS/GF path with a full GF(256) multiplication table and LUT inverse. | `hqc_lab_insintric/src/ref/gf.c`: `gf_mul_table`, `gf_mul`, `gf_inverse`; enabled by `HQC_GF_LUT_MUL=1` |
| 9 | Added benchmark-only RM expand/sum table lookup and degree-bound fast RS roots/error-values. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_expand3_nibble_table`, `expand_and_sum_hvx`; `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_roots`, `compute_error_values`; enabled by `HQC_RM_EXPAND_LUT=1` plus fast RS/GF flags |
| 10 | Added benchmark-only fused RM expand/Hadamard/peak path for full decode. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_decode_one_hvx_fast`, `reed_muller_decode`; `hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh`: `HQC_RM_FUSED_FAST` |
| 11 | Tightened the fast RS algebra path: degree-bound BM auxiliary update, degree-bound z polynomial, and derivative-based Forney denominator. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_elp`, `compute_z_poly`, `compute_error_values`; `hqc_lab_insintric/demos/hqc1_decode_substage_bench.c` |
| 12 | Added benchmark-only HVX Chien root evaluation across the 46 shortened RS support positions. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_roots_hvx`, `rs_support_powers`; Hexagon scripts: `HQC_RS_ROOTS_HVX` |
| 12b | Generalized `compute_roots_hvx` to multi-vector support so HQC-256 (PARAM_N1=90 > 64 lanes) works alongside HQC-128 / HQC-192. Vector count `RS_SUPPORT_VEC_COUNT = CEIL_DIVIDE(PARAM_N1, 64)`. HQC-128 / HQC-192 binaries are byte-identical to the single-vector version (compiler unrolls). NOTE: PQClean's HQC-256 reference uses additive FFT rather than Chien (FFT cost ~256·log2 256 = 2048 GF muls vs Chien 90·29 = 2610), so the scalar FFT default may still win at HQC-256. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_roots_hvx`, `RS_SUPPORT_VEC_COUNT` |
| 13 | Promoted CT-safe default optimizations: fixed-loop HVX roots, fixed-flow RS-local GF arithmetic, derivative error values, and CT RM peak sign recovery. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `rs_gf_mul_ct`, `compute_roots_hvx`, `compute_error_values`; `hqc_lab_insintric/src/ref/reed_muller.c`: `find_peaks_hvx`, `rm_decode_one_hvx_fast`; Hexagon scripts default `HQC_RS_ROOTS_HVX=1`, `HQC_RM_FUSED_FAST=1` |
| 14 | Reduced fastest benchmark-only RM expand loop overhead by unrolling the HQC-128 3-copy word/nibble table lookups. | `hqc_lab_insintric/src/ref/reed_muller.c`: `expand_rm_copies_fast` |
| 15 | Reduced fastest benchmark-only RM Hadamard loop overhead by unrolling the seven fixed HVX butterfly passes. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_hadamard_rows_hvx` |
| 16 | Reduced fastest benchmark-only RS loop overhead by unrolling fixed-bound syndrome, ELP, error scan, and correction loops. | `hqc_lab_insintric/src/ref/reed_solomon.c`: `compute_syndromes_hvx`, `compute_elp`, `compute_error_values`, `correct_errors` |
| 17 | Added the Plan A multiplicity-5 RM expand LUT path for HQC-192/256 by splitting five repeated RM copies into the existing 3-copy nibble LUT plus a new 2-copy nibble LUT. | `hqc_lab_insintric/src/ref/reed_muller.c`: `rm_expand2_nibble_table`, `init_rm_expand2_nibble_table`, `expand_rm_copies_fast` |


## Implemented so far

The original `hqc_lab/` was split into scalar and intrinsic variants.

The first intrinsic pass targeted Reed-Muller decode because it repeatedly processes 128 signed halfword lanes per RS byte:

- `hadamard_hvx`: replaced scalar halfword add/sub in the RM(1,7) Hadamard transform with HVX `vadd.h` and `vsub.h`.
- `find_peaks_hvx`: replaced scalar absolute-value maximum reduction with HVX `vabs.h`, `vmax.h`, and `vror`.
- Reed-Solomon, GF arithmetic, and FFT remain scalar because they are smaller, table-heavy, and more dependency-heavy.

Disassembly confirmed that the intrinsic binary contains HVX instructions such as `vmem`, `vadd`, `vsub`, `vabs`, `vmax`, and `vror`.

The second intrinsic pass removed the scalar even/odd gather inside `hadamard_hvx`. A simulator probe verified this lane transform:

1. `Q6_W_vdeal_VVR(hi, lo, 2)` groups even and odd halfword lanes across the two 128-byte vectors.
2. `Q6_Vh_vdeal_Vh` on each output vector finishes the deinterleave.

The main binary disassembly now shows `vdeal`, `vadd`, `vsub`, `vabs`, `vmax`, and `vror` in the Reed-Muller decode path.

The third intrinsic pass removed the scalar scan from `find_peaks_hvx`. It now uses HVX equality predicates, `vmux`, and `vmin` reduction over explicit index vectors to preserve the reference tie-break rule: choose the smallest position with maximal absolute value.

The fourth intrinsic pass changed `expand_and_sum` for the intrinsic build. Bit extraction is still scalar, but the three RM repetitions are expanded into aligned temporary vectors and summed with HVX halfword adds. This was kept only after the simulator benchmark showed a further reduction.

A stage benchmark was added in `hqc_lab_insintric/demos/hqc1_decode_stage_bench.c` with `hqc_lab_insintric/scripts/run_hqc1_decode_stage_bench_hexagon.sh`. It separates Reed-Muller and Reed-Solomon decode cost so the next optimization target is based on counters rather than guesswork.

The fifth pass used `git/pqc-hqc-hardware/hardware/decap/gfmul.v` as the reference shape for GF multiplication. The new default intrinsic build uses an unrolled `xtime`/xor GF multiplier behind `HQC_USE_GF_HWSTYLE_MUL`. This keeps GF multiplication arithmetic and fixed-flow, unlike the faster LUT experiment. A first looped version was correct but slower; only the unrolled version was promoted after stage and full-decode benchmarks improved.

The sixth pass vectorized RS syndrome computation with HVX. For each received RS byte, it multiplies that byte by the 30 syndrome constants in parallel across halfword lanes, using the same fixed-flow GF multiply shape as the fifth pass. This is now enabled by default in the Hexagon intrinsic scripts through `HQC_HVX_RS_SYNDROME=1`; set `HQC_HVX_RS_SYNDROME=0` to compare against the fifth pass.

The fixture generator now emits a 16-case deterministic random corpus instead of one fixed case. Each benchmark iteration decodes all 16 fixtures, covering random messages and RS-symbol error counts from 0 through `PARAM_DELTA`.

An intrinsic-only substage benchmark was added in `hqc_lab_insintric/demos/hqc1_decode_substage_bench.c` with `hqc_lab_insintric/scripts/run_hqc1_decode_substage_bench_hexagon.sh`. It is compiled with `HQC_ENABLE_SUBSTAGE_BENCH=1` and does not change the production decode path. Timed loops use a small checksum instead of full `memcmp` validation to avoid measuring the verifier more than the target substage.

An opt-in GF multiplication LUT experiment was added behind `HQC_GF_LUT_MUL=1`. This is not the default intrinsic path because it changes GF multiplication from arithmetic constant-time code to data-dependent table lookups. It is useful as a benchmark/engineering tradeoff point, not automatically suitable for production crypto.

The seventh pass targeted the four measured bottlenecks from the substage table:

- `expand_and_sum_hvx` now expands four input bits at a time into packed 16-bit lanes and sums the three RM repetitions with 64-bit arithmetic. This removes the aligned temporary copy vectors and most scalar per-bit stores.
- RS ELP keeps the masked fixed-flow Berlekamp-Massey implementation by default. `HQC_RS_FAST_NON_CT=1` switches to a standard branchy Berlekamp-Massey implementation.
- RS roots now use a shortened-code Chien search over the public `PARAM_N1 = 46` RS positions instead of evaluating the locator over all 256 GF elements with additive FFT. The root test uses a constant-time zero mask.
- The original additive-FFT RS root finder is kept as an opt-in reference path. Set `HQC_RS_ROOTS_FFT=1` in the intrinsic host, full-decode, stage, or substage benchmark scripts to compare against the default Chien search.
- RS error values keep the fixed-loop computation over `PARAM_DELTA = 15` and the 46 shortened RS positions by default. `HQC_RS_FAST_NON_CT=1` switches to an actual-located-error loop.

This seventh pass was the first default constant-time-oriented path. Its default GF multiplication remains arithmetic and fixed-flow. `HQC_GF_LUT_MUL=1` is still available as an opt-in speed experiment, but it uses data-dependent table lookups and should not be treated as constant-time production crypto without a separate threat-model decision.

The seventh pass now has two RS modes:

- Default resistance mode: no extra flag. This keeps fixed-flow ELP and error-value computation.
- Fast non-resistance mode: set `HQC_RS_FAST_NON_CT=1` in the host, Hexagon full-decode, or Hexagon substage benchmark scripts. This mode is for performance comparison only because ELP and error-value work depend on the decoded error structure.

The eighth pass only changes the opt-in GF LUT benchmark path. With `HQC_GF_LUT_MUL=1`, `gf_mul` now initializes and uses a 64 KiB GF(256) multiplication table, and `gf_inverse` uses the `gf_log`/`gf_exp` tables directly. This is intentionally not promoted to the default constant-time-oriented build because it adds data-dependent table access and one-time mutable table initialization, but it is the fastest measured decode benchmark path when side-channel behavior is out of scope.

The ninth pass keeps the default path unchanged and targets the side-channel-relaxed fastest benchmark mode. With `HQC_RM_EXPAND_LUT=1`, RM expand/sum uses a 32 KiB table that maps the three repeated RM codeword nibbles directly to four packed 16-bit sums. In `HQC_RS_FAST_NON_CT=1`, RS roots and error-values now use the actual Berlekamp-Massey degree instead of looping to `PARAM_DELTA` when evaluating the locator and numerator polynomials.

The tenth pass keeps the substage benchmark path unchanged and adds a full-decode-only fused RM path behind `HQC_RM_FUSED_FAST=1`. It expands one RM block, runs the HVX Hadamard transform, and performs the HVX peak reduction inside one inlined function. This removes one function boundary and the intermediate handoff between the generic expand and Hadamard helpers in the fastest side-channel-relaxed benchmark mode.

The eleventh pass targets the remaining RS algebra in the side-channel-relaxed fastest mode. The current software path keeps the branchy Berlekamp-Massey shape instead of switching to the heavier inversionless BM architecture from Wu 2015, because the GF table path already makes inversions cheap and the inversionless update would multiply whole polynomials every iteration. The adopted changes are narrower: track the actual degree of the BM auxiliary polynomial `b`, compute `z(x)` only to the actual locator degree, and use the formal derivative of `sigma(x)` as the Forney denominator for error values. This reuses the Wu/Forney observation that the derivative/odd locator terms can replace the explicit product over all other error locators.

The twelfth pass adds an opt-in HVX Chien search for the fastest mode. It precomputes the powers of the 46 shortened RS support points into aligned 64-lane vectors and evaluates `sigma(x)` as a vector sum of `sigma[j] * x^j`. This deliberately does not use the GF table path inside the vector operation; the measured win comes from evaluating all support positions in one HVX vector, while keeping the scalar Chien path as the default and as the fallback when `HQC_RS_ROOTS_HVX=0`.

The thirteenth pass keeps the default side-channel-resistant strategy but moves the safe parts of the faster experiments into the default Hexagon build. The HVX Chien backend now has a default fixed-flow branch that always evaluates all `PARAM_DELTA + 1` locator coefficients, avoids the `sigma[j] != 0` branch, and writes only the public 46 shortened-support positions. Reed-Solomon now uses RS-local inlined fixed-flow GF multiply/square/inverse helpers when `HQC_USE_GF_HWSTYLE_MUL=1` and `HQC_GF_LUT_MUL=0`, removing call overhead without introducing secret-indexed tables. Default RS error values now use the formal derivative denominator in fixed `PARAM_DELTA` loops instead of multiplying over the other located errors. RM peak handling no longer loads `transform[peak_pos]` in the default path; it recovers the peak sign with vector compare/mux/reduction. The side-channel-relaxed fastest path keeps its old fused RM indexed load under `HQC_RS_FAST_NON_CT=1`, so the fastest benchmark result is unchanged.

Current run note: `hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh` now runs the fastest path by default, so the old fastest env flags are no longer needed. Use `HQC1_BENCH_ITERS=10 ./hqc_lab_insintric/scripts/run_hqc1_decode_bench_hexagon.sh` for the standard simulator benchmark.

## Benchmark results

When a candidate is correctness-safe and improves simulator Pcycles, this file should be updated with the new result before moving on.

All runs used `demos/hqc1_decode_bench.c`, the same fixture, and `HQC1_BENCH_ITERS=100` unless noted.

| Variant | Result | Total insns | Total Pcycles | Wall time |
| --- | --- | ---: | ---: | ---: |
| `hqc_lab_scalar` | PASS | 87,945,846 | 97,724,244 | 24.55s |
| `hqc_lab_insintric` first pass | PASS | 82,938,131 | 91,779,186 | 24.52s |
| `hqc_lab_insintric` second pass | PASS | 70,347,931 | 79,345,386 | 21.15s |
| `hqc_lab_insintric` third pass | PASS | 62,728,180 | 72,489,072 | 19.87s |
| `hqc_lab_insintric` fourth pass | PASS | 60,567,603 | 69,123,735 | 16.82s |
| `hqc_lab_insintric` fifth pass, hardware-style GF | PASS | 44,280,957 | 64,426,629 | not timed |
| `hqc_lab_insintric` sixth pass, HVX RS syndrome | PASS | 35,356,810 | 50,178,084 | not timed |
| `hqc_lab_insintric` fourth pass + opt-in GF LUT | PASS | 27,502,984 | 36,925,356 | 9.30s |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | PASS | 24,248,761 | 31,991,607 | not timed |

One-iteration runs were used to estimate fixed simulator/boot overhead:

| Variant | Result | Total insns | Total Pcycles | Wall time |
| --- | --- | ---: | ---: | ---: |
| `hqc_lab_scalar`, 1 iter | PASS | 2,683,701 | 3,453,807 | 2.39s |
| `hqc_lab_insintric`, 1 iter | PASS | 2,634,302 | 3,395,586 | 4.44s |
| `hqc_lab_insintric` second pass, 1 iter | PASS | 2,508,400 | 3,271,248 | 1.93s |
| `hqc_lab_insintric` third pass, 1 iter | PASS | 2,433,835 | 3,204,651 | not timed |
| `hqc_lab_insintric` fourth pass, 1 iter | PASS | 2,413,044 | 3,171,951 | not timed |
| `hqc_lab_insintric` fifth pass, hardware-style GF, 1 iter | PASS | 2,250,132 | 3,124,677 | not timed |
| `hqc_lab_insintric` sixth pass, HVX RS syndrome, 1 iter | PASS | 2,173,813 | 3,001,452 | not timed |
| `hqc_lab_insintric` fourth pass + opt-in GF LUT, 1 iter | PASS | 2,083,567 | 2,851,770 | not timed |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT, 1 iter | PASS | 2,063,872 | 2,821,491 | not timed |

Approximate per-decode cost from `(100-iter Pcycles - 1-iter Pcycles) / 99`:

| Variant | Estimated Pcycles/decode |
| --- | ---: |
| Scalar | 952,226 |
| First intrinsic pass | 892,764 |
| Second intrinsic pass | 768,426 |
| Third intrinsic pass | 699,843 |
| Fourth intrinsic pass | 666,180 |
| Fifth pass, hardware-style GF | 619,212 |
| Sixth pass, HVX RS syndrome | 476,532 |
| Fourth intrinsic pass + opt-in GF LUT | 344,178 |
| Sixth pass + opt-in GF LUT | 294,648 |

The first intrinsic pass was about 6.2% faster than scalar by estimated Pcycles/decode. The fourth pass was about 30.0% faster than scalar. The fifth pass was about 35.0% faster than scalar. In this older single-fixture table, the sixth pass is about 50.0% faster than scalar and about 23.0% faster than the fifth pass by estimated Pcycles/decode. The sixth-pass opt-in GF LUT combo is about 69.1% faster than scalar and about 38.2% faster than the sixth pass, but carries the side-channel caveat above. The current seventh-pass result is measured in the 16-fixture corpus table below. Wall time is less reliable than Pcycles because `hexagon-sim` startup and simulation overhead are significant.

Stage benchmark snapshots:

| Variant | Stage | Iters | Result | Total Pcycles |
| --- | --- | ---: | --- | ---: |
| Fourth pass default | RM | 100 | PASS | 15,658,983 |
| Fourth pass default | RS | 100 | PASS | 57,289,110 |
| Fifth pass, hardware-style GF | RS | 100 | PASS | 52,545,096 |
| Sixth pass, HVX RS syndrome | RS | 100 | PASS | 38,153,673 |
| Fourth pass + opt-in GF LUT | RS | 100 | PASS | 24,768,837 |
| Sixth pass + opt-in GF LUT | RS | 100 | PASS | 19,785,558 |

Corpus benchmark snapshots:

These runs used the 16-fixture corpus. `HQC1_BENCH_ITERS=10` means 160 total decodes; `HQC1_BENCH_ITERS=1` means 16 total decodes. Estimated Pcycles/decode uses `(10-iter Pcycles - 1-iter Pcycles) / (9 * 16)`.

| Variant | Iters | Fixtures | Result | Total insns | Total Pcycles |
| --- | ---: | ---: | --- | ---: | ---: |
| `hqc_lab_scalar` | 10 | 16 | PASS | 139,903,064 | 155,173,506 |
| `hqc_lab_scalar` | 1 | 16 | PASS | 15,658,494 | 17,831,082 |
| `hqc_lab_insintric` sixth pass | 10 | 16 | PASS | 55,750,933 | 79,085,736 |
| `hqc_lab_insintric` sixth pass | 1 | 16 | PASS | 7,257,851 | 10,243,392 |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | 10 | 16 | PASS | 37,977,434 | 49,986,384 |
| `hqc_lab_insintric` sixth pass + opt-in GF LUT | 1 | 16 | PASS | 5,481,600 | 7,335,336 |
| `hqc_lab_insintric` seventh pass CT | 10 | 16 | PASS | 46,334,532 | 66,027,102 |
| `hqc_lab_insintric` seventh pass CT | 1 | 16 | PASS | 6,311,098 | 8,930,406 |
| `hqc_lab_insintric` seventh pass CT + opt-in GF LUT | 10 | 16 | PASS | 28,340,550 | 36,530,676 |
| `hqc_lab_insintric` seventh pass CT + opt-in GF LUT | 1 | 16 | PASS | 4,512,796 | 5,982,540 |
| `hqc_lab_insintric` seventh pass fast non-CT | 10 | 16 | PASS | 32,359,087 | 45,166,485 |
| `hqc_lab_insintric` seventh pass fast non-CT | 1 | 16 | PASS | 4,912,694 | 6,843,132 |
| `hqc_lab_insintric` seventh pass fast non-CT + opt-in GF LUT | 10 | 16 | PASS | 20,979,590 | 26,504,094 |
| `hqc_lab_insintric` seventh pass fast non-CT + opt-in GF LUT | 1 | 16 | PASS | 3,775,854 | 4,978,710 |
| `hqc_lab_insintric` eighth pass fast non-CT + GF table LUT | 10 | 16 | PASS | 17,790,289 | 21,116,241 |
| `hqc_lab_insintric` eighth pass fast non-CT + GF table LUT | 1 | 16 | PASS | 4,034,876 | 5,080,200 |
| `hqc_lab_insintric` ninth pass fast non-CT + GF table LUT + RM LUT | 10 | 16 | PASS | 11,192,310 | 14,187,261 |
| `hqc_lab_insintric` ninth pass fast non-CT + GF table LUT + RM LUT | 1 | 16 | PASS | 3,485,005 | 4,534,398 |
| `hqc_lab_insintric` tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 10 | 16 | PASS | 11,086,171 | 14,111,097 |
| `hqc_lab_insintric` tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 1 | 16 | PASS | 3,464,978 | 4,517,850 |
| `hqc_lab_insintric` eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 10 | 16 | PASS | 10,398,737 | 13,185,531 |
| `hqc_lab_insintric` eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 1 | 16 | PASS | 3,396,303 | 4,425,477 |
| `hqc_lab_insintric` twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 10 | 16 | PASS | 9,812,288 | 12,855,702 |
| `hqc_lab_insintric` twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 1 | 16 | PASS | 3,353,940 | 4,417,002 |
| `hqc_lab_insintric` thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 10 | 16 | PASS | 35,648,131 | 43,766,676 |
| `hqc_lab_insintric` thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 1 | 16 | PASS | 5,287,097 | 6,792,540 |
| `hqc_lab_insintric - 13pass backup` CT-only refactor verification | 10 | 16 | PASS | 35,463,810 | 43,082,616 |
| `hqc_lab_insintric - 13pass backup` CT-only refactor verification | 1 | 16 | PASS | 5,268,232 | 6,724,080 |
| `hqc_lab_insintric` thirteenth pass fastest non-CT regression check | 10 | 16 | PASS | 9,812,288 | 12,855,702 |
| `hqc_lab_insintric` thirteenth pass fastest non-CT regression check | 1 | 16 | PASS | 3,353,940 | 4,417,002 |
| `hqc_lab_insintric` fourteenth pass fastest non-CT + unrolled RM expand LUT | 10 | 16 | PASS | 8,373,094 | 11,653,638 |
| `hqc_lab_insintric` fourteenth pass fastest non-CT + unrolled RM expand LUT | 1 | 16 | PASS | 3,219,674 | 4,307,034 |
| `hqc_lab_insintric` fifteenth pass fastest non-CT + unrolled RM expand LUT + unrolled RM Hadamard | 10 | 16 | PASS | 8,277,490 | 11,587,602 |
| `hqc_lab_insintric` fifteenth pass fastest non-CT + unrolled RM expand LUT + unrolled RM Hadamard | 1 | 16 | PASS | 3,210,182 | 4,300,614 |
| `hqc_lab_insintric` sixteenth pass fastest non-CT + unrolled RM + unrolled RS fixed loops | 10 | 16 | PASS | 8,049,062 | 11,272,635 |
| `hqc_lab_insintric` sixteenth pass fastest non-CT + unrolled RM + unrolled RS fixed loops | 1 | 16 | PASS | 3,194,073 | 4,286,103 |

| Variant | Corpus-estimated Pcycles/decode |
| --- | ---: |
| Scalar | 953,767 |
| Sixth pass, HVX RS syndrome | 478,072 |
| Sixth pass + opt-in GF LUT | 296,188 |
| Seventh pass CT | 396,505 |
| Seventh pass CT + opt-in GF LUT | 212,140 |
| Seventh pass fast non-CT | 266,134 |
| Seventh pass fast non-CT + opt-in GF LUT | 149,482 |
| Eighth pass fast non-CT + GF table LUT | 111,361 |
| Ninth pass fast non-CT + GF table LUT + RM LUT | 67,034 |
| Tenth pass fast non-CT + GF table LUT + RM LUT + fused RM | 66,620 |
| Eleventh pass fast non-CT + GF table LUT + RM LUT + fused RM | 60,834 |
| Twelfth pass fast non-CT + GF table LUT + RM LUT + fused RM + HVX roots | 58,602 |
| Thirteenth pass CT default + CT-HVX roots + arithmetic fused RM | 256,765 |
| Fourteenth pass fastest non-CT + unrolled RM expand LUT | 51,018 |
| Fifteenth pass fastest non-CT + unrolled RM expand LUT + unrolled RM Hadamard | 50,604 |
| Sixteenth pass fastest non-CT + unrolled RM + unrolled RS fixed loops | 48,518 |


On the corpus benchmark, the seventh-pass default CT path is about 58.4% faster than scalar and about 17.1% faster than the sixth pass. The thirteenth-pass default CT path is about 73.1% faster than scalar, about 35.2% faster than the seventh-pass CT path, and about 46.3% faster than the sixth pass. The seventh-pass fast non-CT path is about 72.1% faster than scalar and about 44.3% faster than the sixth pass, but it leaks decoded-error structure through branchy RS control flow. The sixteenth-pass fastest measured combination is 48,518 Pcycles/decode after unrolling the HQC-128 RM expand LUT lookups, the fixed seven-pass RM Hadamard loop, and selected fixed-bound RS loops. It combines branchy RS leakage with data-dependent GF/RM table lookups, HVX root evaluation, and the fused RM benchmark path, so it remains benchmark-only.

Default intrinsic substage snapshots:

These runs used the 16-fixture corpus. `HQC1_BENCH_ITERS=10` and `HQC1_BENCH_ITERS=1` were both run for every substage. Estimated cost is `(10-iter Pcycles - 1-iter Pcycles) / (9 * operation_count_per_iteration)`. RM substages report both per RM block and per full decode, where one decode has `PARAM_N1 = 46` RM blocks.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost |
| --- | ---: | ---: | ---: |
| RM expand/sum | 36,534,192 | 20,038,749 | 2,490/block; 114,552/decode |
| RM Hadamard | 21,232,335 | 18,509,763 | 411/block; 18,907/decode |
| RM find peak | 19,112,301 | 18,296,346 | 123/block; 5,666/decode |
| RS syndrome | 18,802,773 | 18,266,238 | 3,726/decode |
| RS ELP | 35,757,174 | 19,961,721 | 109,691/decode |
| RS roots/FFT | 33,920,784 | 19,777,914 | 98,214/decode |
| RS Z polynomial | 20,092,398 | 18,395,526 | 11,784/decode |
| RS error values | 39,174,546 | 20,303,742 | 131,047/decode |
| RS correction | 18,277,167 | 18,213,798 | 440/decode |

At that snapshot, the dominant default-intrinsic costs were RS error values, RM expand/sum, RS ELP, and RS roots/FFT. RM Hadamard, RM peak search, RS syndrome, and final correction were no longer primary targets.

Seventh-pass CT substage snapshots with the default non-LUT fixed-flow GF path:

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 24,532,173 | 16,482,330 | 1,215/block; 55,902/decode | 51.2% lower |
| RS ELP | 33,139,155 | 17,343,702 | 109,691/decode | unchanged |
| RS roots/Chien | 27,627,885 | 16,792,407 | 75,246/decode | 23.4% lower |
| RS error values | 36,556,527 | 17,685,927 | 131,046/decode | unchanged |

Thirteenth-pass CT default substage snapshots with fixed-flow RS-local GF helpers, fixed-loop HVX roots, derivative error values, and no default secret-indexed RM peak load:

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs seventh-pass CT |
| --- | ---: | ---: | ---: | ---: |
| RS ELP | 24,466,578 | 12,678,789 | 81,860/decode | 25.4% lower |
| RS roots/HVX Chien | 11,920,686 | 11,424,264 | 3,447/decode | 95.4% lower |
| RS error values | 25,640,238 | 12,796,875 | 89,190/decode | 31.9% lower |

The `hqc_lab_insintric - 13pass backup` CT-only refactor rerun kept these checkpoints within or below the recorded README numbers: RS ELP 1-iter `12,478,239`, RS roots 1-iter `11,223,510`, and RS error values 1-iter `12,567,417`, all PASS.

Two full-decode isolation runs were used to check which default CT pieces mattered. With the thirteenth-pass RS arithmetic and error-value changes but both `HQC_RS_ROOTS_HVX=0` and `HQC_RM_FUSED_FAST=0`, full decode was 316,222 Pcycles/decode. With `HQC_RS_ROOTS_HVX=1` and `HQC_RM_FUSED_FAST=0`, it was 258,844 Pcycles/decode. With both thirteenth-pass defaults enabled, it was 256,765 Pcycles/decode. Most of the full-decode win therefore comes from fixed-flow HVX roots and RS arithmetic/error-value changes; arithmetic fused RM is a small default win after the CT peak-sign fix.

Seventh-pass fast non-CT substage snapshots with opt-in GF LUT and non-fixed-flow RS/GF behavior:

These runs used `HQC_RS_FAST_NON_CT=1` and `HQC_GF_LUT_MUL=1`. They are useful for performance comparison, but not for a side-channel-resistant default.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 16,544,778 | 8,494,731 | 1,215/block; 55,903/decode | 51.2% lower |
| RS ELP | 11,498,550 | 7,990,782 | 24,360/decode | 77.8% lower |
| RS roots/Chien | 12,022,890 | 8,043,048 | 27,638/decode | 71.9% lower |
| RS error values | 11,055,858 | 7,947,021 | 21,589/decode | 83.5% lower |

Ninth-pass fast non-CT substage snapshots with GF table LUT, RM expand LUT, and degree-bound RS:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, and `HQC_RM_EXPAND_LUT=1`.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs eighth/seventh fast bottleneck |
| --- | ---: | ---: | ---: | ---: |
| RM expand/sum | 9,192,684 | 6,170,457 | 456/block; 20,988/decode | 62.4% lower vs eighth-pass fast |
| RS roots/Chien | 7,318,650 | 5,983,608 | 9,271/decode | 66.5% lower vs seventh-pass fast |
| RS error values | 6,990,762 | 5,951,475 | 7,217/decode | 66.6% lower vs seventh-pass fast |

Eleventh-pass fast non-CT RS algebra substage snapshots:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, and `HQC_RM_EXPAND_LUT=1`. The full fastest decode also used `HQC_RM_FUSED_FAST=1`, but the RS substage benchmark does not exercise that RM-only flag.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior fast substage |
| --- | ---: | ---: | ---: | ---: |
| RS ELP/Berlekamp-Massey | 6,989,178 | 5,854,563 | 7,879/decode | 29.8% lower vs degree-unbounded fast BM update |
| RS z polynomial | 5,853,102 | 5,688,750 | 1,141/decode | 61.5% lower vs fixed-loop fast z |
| RS error values | 6,592,524 | 5,727,954 | 6,004/decode | 16.8% lower vs explicit denominator product |

Twelfth-pass fast non-CT HVX roots substage snapshot:

These runs used `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, and `HQC_RS_ROOTS_HVX=1`.

| Substage | 10-iter Pcycles | 1-iter Pcycles | Estimated cost | Change vs prior fast substage |
| --- | ---: | ---: | ---: | ---: |
| RS roots/HVX Chien | 5,930,871 | 5,623,881 | 2,132/decode | 77.0% lower vs scalar degree-bound Chien |

## Next optimization candidates

1. Done: remove scalar even/odd gather from `hadamard_hvx`.
   The first pass copied 128 halfwords into temporary `even[]` and `odd[]` arrays each Hadamard pass. The second pass replaced this with verified HVX deal/deinterleave instructions.

2. Partially done: restructure `expand_and_sum`.
   The fourth pass still extracts bits with scalar shifts and masks, but it moves the summation across RM repetitions to HVX. A more aggressive version would need either a verified LUT path or a data-layout change.

3. Done: make `find_peaks_hvx` return the index with vector logic.
   The third pass uses vector index selection and min reduction to preserve tie-breaking semantics.

4. Add stage-level timing or counters.
   Added at RM-vs-RS granularity. A deeper version can split RM expansion, Hadamard, peak search, syndrome computation, ELP, FFT roots, and error values.

5. Done: port the hardware GF multiplier shape to C and unroll it.
   The looped form was correct but slower. The unrolled form is now the default intrinsic GF path because it improved RS stage Pcycles from 57,289,110 to 52,545,096 and full estimated decode cost from 666,180 to 619,212 Pcycles/decode without adopting data-dependent table lookup.

6. Done: vectorize RS syndrome computation with HVX.
   The syndrome step matches HVX well because it computes 30 independent GF products per RS byte. The sixth pass improved RS stage Pcycles from 52,545,096 to 38,153,673 and full estimated decode cost from 619,212 to 476,532 Pcycles/decode.

7. Done: target the current top four bottlenecks.
   The constant-time-oriented seventh pass keeps the safe wins: RM packed expansion and shortened Chien root search over public RS positions. The branchy BM and actual-error value experiments are still available behind `HQC_RS_FAST_NON_CT=1`, but the default path leaves them off because they make RS control flow depend on decoded error structure.

8. Done: promote CT-safe versions of the strongest measured default-path ideas.
   The thirteenth pass makes HVX Chien roots default only after removing the degree-dependent loop and coefficient-dependent branch from the default path. It also keeps GF arithmetic fixed-flow while inlining it into RS, uses the derivative denominator for error values with fixed `PARAM_DELTA` loops, and removes the default secret-indexed RM peak-value load. The fastest side-channel-relaxed path is still available and unchanged in measured Pcycles/decode.

9. Done: unroll the fastest HQC-128 RM expand LUT loop.
   The fourteenth pass keeps the same 3-copy nibble table mapping but removes the fixed four-word and eight-nibble loops in `expand_rm_copies_fast`. A trial vector peak-sign rewrite was rejected because it passed correctness but regressed full decode from the post-audit 57,507 Pcycles/decode baseline to about 58,473 Pcycles/decode.

10. Done: unroll the fixed seven-pass RM Hadamard loop.
    The fifteenth pass keeps the same HVX deal/deinterleave butterfly operations but expands the fixed loop body. A trial hoist of RM expand table initialization was rejected because it passed correctness but regressed full decode from 51,018 to about 51,306 Pcycles/decode.

11. Done: unroll selected fastest-path RS fixed loops.
    The sixteenth pass keeps the same branchy fastest non-CT RS algebra but asks the compiler to unroll fixed-bound syndrome, Berlekamp-Massey outer, error-position scan, and correction loops. A trial unroll of the `z` zeroing loop was rejected because it produced no full-decode improvement.

12. Done: add a multiplicity-5 RM expand LUT path for HQC-192/256.
    The seventeenth pass fixes the HQC-192/256 fastest-path scaling issue by replacing the generic five-copy arithmetic RM expansion with a compact table split: existing 3-copy nibble LUT plus a new 2-copy nibble LUT. This avoids a direct `16^5` table, keeps the memory footprint small, leaves HQC-128 unchanged, and was validated by simulator plus QRD8650 and HDK8550 Android hardware runs.

## Confidence notes

The safe second-pass step was candidate 1: replacing the scalar even/odd gather with HVX deal/deinterleave. It was narrower than rewriting `expand_and_sum`, kept the same data representation, and directly fixed a measured weakness in the first intrinsic pass.

The stage benchmark shows RS/GF is now the dominant default cost after RM intrinsic work. The remaining high-risk candidates are:

- A fuller `expand_and_sum` rewrite using `vlut16`, gather/scatter, or a data-layout change. It may no longer be the best next target because RM is no longer dominant.
- A vectorized RS/GF path. This has higher upside, but proving correctness across all GF inputs and avoiding data-dependent lookup side channels are the main loopholes.
- A deeper RS/RM stage benchmark. Done at substage granularity for the current intrinsic default. After the thirteenth pass, RS roots are no longer a default bottleneck; the remaining default targets are RS error values, RS ELP, RM expand/sum, and z polynomial, not RS syndrome or RM Hadamard.

The hardware-style GF path is the safest measured scalar improvement from the paper/Verilog review: it is correct for all 256x256 GF input pairs in a host self-test, passes host and simulator decode, and is fixed-flow arithmetic. The HVX syndrome path builds on that same arithmetic shape and is enabled because it maps independent syndrome lanes directly to HVX lanes. The opt-in GF LUT path is still factually faster on the simulator, but it should stay opt-in until the threat model accepts data-dependent table lookup on RS intermediate values.

For the seventh pass, confidence is scoped to correctness on the 16-fixture corpus, measured Hexagon simulator Pcycles, and the implemented constant-time strategy. The loopholes found during strategy review were side-channel behavior, uncorrectable-codeword behavior, shortened-RS root mapping, accidental promotion of the GF LUT path, and accidental promotion of branchy RS code. The fixes applied were: keep masked fixed-flow ELP and error-value computation in the default build, keep correction tests across 0..15 RS-symbol errors, make the root search cover exactly the public shortened RS positions `0..PARAM_N1-1`, use a constant-time root-zero test, leave `HQC_GF_LUT_MUL=1` as an explicitly opt-in benchmark path, and gate branchy RS code behind `HQC_RS_FAST_NON_CT=1`.

For the eighth pass, confidence is scoped to the benchmark-only side-channel-relaxed mode requested for speed comparison. The loopholes checked were incorrect GF table generation, inverse behavior for zero, one-time table initialization overhead, accidental changes to the default CT path, and regression against the previous fastest measured result. The fixes/checks were: keep the table path behind `HQC_GF_LUT_MUL=1`, keep `gf_inverse(0) = 0`, verify `gf_mul` against the 256x256 LUT reference self-test, run host decode in both default and fast LUT modes, and rerun Hexagon simulator 1-iter and 10-iter corpus benchmarks before recording the result.

For the ninth pass, confidence is scoped to the same side-channel-relaxed benchmark mode. The loopholes checked were accidental default-path changes, RM table mapping mistakes, RS degree-bound correctness, and substage/full-decode regression. The fixes/checks were: gate RM table lookup behind `HQC_RM_EXPAND_LUT=1`, leave default Hexagon and host decode passing, run host fast decode over the 16-fixture corpus, run Hexagon full-decode 1-iter and 10-iter benchmarks with the fastest flags, and rerun the affected RM expand, RS roots, and RS error-values substage benchmarks.

For the tenth pass, confidence is scoped to the side-channel-relaxed full-decode benchmark only. The loopholes checked were accidental default-path changes, fused RM semantic drift, substage benchmark contamination, and regression against the ninth-pass fastest result. The fixes/checks were: gate the fused path behind `HQC_RM_FUSED_FAST=1`, leave the existing expand/Hadamard/peak helpers and substage benchmark path unchanged, run default host decode over the 16-fixture corpus, run default Hexagon 1-iter decode, and run Hexagon 1-iter and 10-iter corpus benchmarks with `HQC_RS_FAST_NON_CT=1`, `HQC_GF_LUT_MUL=1`, `HQC_RM_EXPAND_LUT=1`, and `HQC_RM_FUSED_FAST=1`.

For the eleventh pass, confidence is scoped to the side-channel-relaxed RS fast path. The loopholes checked were inversionless-BM normalization cost, stale high-degree `z` coefficients, incorrect Forney denominator scaling, benchmark helper signature drift, accidental default-path changes, and regression against the tenth-pass fastest result. The fixes/checks were: keep inversionless BM as a rejected strategy for this software/GF-LUT setting, keep the default CT-oriented ELP and z/error-value paths unchanged, clear unused fast `z` coefficients, pass `sigma` explicitly into the error-value helper, verify both default and fast host decode over the 16-fixture corpus, run default Hexagon 1-iter decode, rerun the affected RS ELP/z/error-values substage benchmarks, and rerun Hexagon full-decode 1-iter and 10-iter fastest benchmarks.

For the twelfth pass, confidence is scoped to the side-channel-relaxed Hexagon HVX roots experiment. The loopholes checked were support-point ordering, invalid lanes 46..63, table initialization overhead, GF table versus HVX arithmetic tradeoff, accidental default-path changes, and full-decode regression. The fixes/checks were: precompute powers for exactly `gf_exp[PARAM_GF_MUL_ORDER - i]`, ignore lanes beyond `PARAM_N1`, gate the path behind `HQC_RS_ROOTS_HVX=1`, keep scalar Chien as the default, run host fast decode, run default Hexagon 1-iter decode, rerun RS roots substage 1-iter and 10-iter with the flag, and rerun Hexagon full-decode 1-iter and 10-iter fastest benchmarks with `HQC_RS_ROOTS_HVX=1`.

For the thirteenth pass, confidence is scoped to software constant-time behavior, correctness on the 16-fixture corpus, and Hexagon simulator Pcycles. It is not a formal physical side-channel proof. The loopholes checked were accidental data-dependent GF/RM tables in the default path, degree-dependent HVX roots, coefficient-dependent HVX roots branches, secret-indexed RM peak-value loads, implementation-defined signed-shift masks, fastest-path regression, and accidental promotion of branchy RS code. The fixes/checks were: keep `HQC_GF_LUT_MUL=0` in the default scripts, evaluate all `PARAM_DELTA + 1` coefficients in default `compute_roots_hvx`, keep the branchy degree-bound roots only under `HQC_RS_FAST_NON_CT=1`, recover RM peak sign with vector compare/mux/reduction in the default path, replace several default masks with unsigned helpers, keep the fused RM indexed load only under `HQC_RS_FAST_NON_CT=1`, run host decode, run default Hexagon 1-iter and 10-iter full-decode benchmarks, rerun RS ELP/roots/error-values substage 1-iter and 10-iter benchmarks, and rerun the fastest non-CT 1-iter and 10-iter benchmarks to confirm it remains 58,602 Pcycles/decode.

For the fourteenth pass, confidence is scoped to the benchmark-only fastest non-CT path. The loopholes checked were substage-only wins that do not improve full decode, accidental HQC-192/256 breakage from an HQC-128-specific unroll, and misleading wins from rejected RM peak changes. The accepted RM expand unroll passed HQC-128 1-iter and 10-iter full decode, improved the corpus estimate to 51,018 Pcycles/decode, passed the affected RM expand substage, passed HQC-192/256 1-iter decode checks, and left the data-dependent lookup caveat unchanged.

For the fifteenth pass, confidence is scoped to the same benchmark-only fastest non-CT path. The loopholes checked were code-size/scheduling regression from unrolling the Hadamard hardware loop and accidental breakage for HQC-192/256 multiplicity-5 decode. The accepted Hadamard unroll passed HQC-128 1-iter and 10-iter full decode, improved the corpus estimate to 50,604 Pcycles/decode, passed the affected RM Hadamard substage, and passed HQC-192/256 1-iter decode checks.

For the sixteenth pass, confidence is scoped to the same benchmark-only fastest non-CT path and Hexagon simulator Pcycles. The loopholes checked were code-size regressions from pragma unrolling, substage-only wins, and parameter-set breakage. The accepted RS loop unrolls passed HQC-128 1-iter and 10-iter full decode, improved the corpus estimate to 48,518 Pcycles/decode, passed the affected RS syndrome/ELP/error-values/correct substages, and passed HQC-192/256 1-iter decode checks. The `rs_z` unroll trial was rejected because it produced no measurable full-decode gain.

For the seventeenth pass, confidence is scoped to the benchmark-only fastest non-CT HQC-192/256 path and measured device latency. The loopholes checked were direct 5-copy LUT size, table-cache regression, accidental HQC-128 regression, simulator-only wins, FastRPC/Android hardware mismatch, and qprof perturbing cDSP clock state. The accepted 3+2 LUT split passed simulator substage and full-decode checks, left HQC-128 simulator results unchanged, improved HQC-192/256 simulator full decode by about 2.27x/2.29x, improved QRD8650 direct NPU latency to about 41.21 us/62.76 us, and improved HDK8550 direct NPU latency to about 50.75 us/77.08 us. qprof was kept for energy/counter evidence only because it can perturb post-run direct latency until reboot.
