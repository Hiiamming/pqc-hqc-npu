# HQC-128 ARM64 scalar board baseline

This folder builds a CPU-only ARM64 Linux baseline for the Qualcomm board.
It does not use FastRPC, cDSP, Hexagon, or HVX. Copy the produced executable to
the board and run it directly.

Build locally:

```sh
bash scalar_on_board/build_arm64.sh
```

Optional iteration count:

```sh
HQC128_BENCH_ITERS=10000 bash scalar_on_board/build_arm64.sh
```

Upload from your machine:

```sh
scp -P 2222 -i "C:\Users\ADMIN\Downloads\qdc_id_2026-5-13_317.pem" \
  scalar_on_board/build/hqc128_decode_bench_arm64 \
  root@localhost:/data/local/tmp/QDC_files/scalar_on_board/
```

Run on the board:

```sh
cd /data/local/tmp/QDC_files/scalar_on_board
chmod +x hqc128_decode_bench_arm64
./hqc128_decode_bench_arm64
```

Expected output includes `result=PASS` plus elapsed time and average time per
HQC-128 decode.
