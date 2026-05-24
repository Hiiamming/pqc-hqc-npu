# Android Device Measurement

Build and measure CPU scalar plus fastest NPU:

```sh
ADB=/path/to/adb scripts/measure_android_decode.sh
```

Measure CT NPU:

```sh
ADB=/path/to/adb scripts/measure_android_ct_npu.sh
```

Run both:

```sh
ADB=/path/to/adb scripts/measure_android_all.sh
```

The direct path uses `scripts/measure_board_energy.sh` on-device. The qprof path is still separate and is run by the measurement scripts only for the qprof rows, because qprof itself adds overhead and should not be mixed into direct-energy runs.

Root-level compatibility wrappers were removed; use the `scripts/` entrypoints directly.
