# HQC-128 FastRPC scalar cDSP baseline

This folder ports the same scalar decoder used by `scalar_on_board` to cDSP via
FastRPC. The benchmark fixture is compiled into `libhqc_skel.so`; the host calls
one RPC method and times it around the full DSP-side loop.

Build locally:

```sh
bash hqc_fastrpc_scalar/build.sh
```

Upload:

```sh
ssh -p 2222 -i "C:\Users\ADMIN\Downloads\qdc_id_2026-5-13_317.pem" \
  root@localhost "mkdir -p /data/local/tmp/QDC_files/hqc_fastrpc_scalar"

scp -P 2222 -i "C:\Users\ADMIN\Downloads\qdc_id_2026-5-13_317.pem" \
  hqc_fastrpc_scalar/build/hqc_host \
  hqc_fastrpc_scalar/build/libhqc_skel.so \
  hqc_fastrpc_scalar/build/testsig-0xaa3ec42e.so \
  root@localhost:/data/local/tmp/QDC_files/hqc_fastrpc_scalar/
```

Run on the board:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_scalar
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host
```

Optional iteration count:

```sh
./hqc_host 10000
```

Expected output includes `result=PASS` plus elapsed time and average time per
HQC-128 decode.

