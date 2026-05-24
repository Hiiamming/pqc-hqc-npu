# HQC FastRPC HVX intrinsic cDSP variant

This folder ports the `hqc_lab_insintric` HVX decoder to cDSP via FastRPC. The
benchmark fixture is compiled into `libhqc_skel.so`; the host calls one RPC
method and times it around the full DSP-side loop.

The IDL uses `remote_handle64`, and the host opens the cDSP explicitly with
`hqc_URI CDSP_DOMAIN`. This matches the Qualcomm SDK examples and avoids the
implicit singleton open path returning `AEE_EINVHANDLE` on RB3 Gen 2 Linux.

The DSP build enables:

- `HQC_USE_HVX_INTRINSICS=1`
- `HQC_USE_HVX_RS_SYNDROME=1`
- `-mhvx -mhvx-length=128B`

The default Hexagon target is `v68`, which is the conservative choice for
QCS6490/RB3 Gen 2 class devices. Override it only when the board firmware and
SDK explicitly support a newer target:

```sh
HEXAGON_ARCH=v73 bash hqc_fastrpc_intrinsic/build.sh
```

Build locally:

```sh
bash hqc_fastrpc_intrinsic/build.sh
```

The default parameter set is HQC-128. Build one parameter set at a time:

```sh
HQC_PARAM_LEVEL=128 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=192 bash hqc_fastrpc_intrinsic/build.sh
HQC_PARAM_LEVEL=256 bash hqc_fastrpc_intrinsic/build.sh
```

Each build overwrites `build/hqc_host` and `build/libhqc_skel.so`.

Upload:

```sh
ssh -p 2222 -i "C:\Users\ADMIN\Downloads\qdc_id_2026-5-13_317.pem" \
  root@localhost "mkdir -p /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic"

scp -P 2222 -i "C:\Users\ADMIN\Downloads\qdc_id_2026-5-13_317.pem" \
  hqc_fastrpc_intrinsic/build/hqc_host \
  hqc_fastrpc_intrinsic/build/libhqc_skel.so \
  hqc_fastrpc_intrinsic/build/testsig-0xaa3ec42e.so \
  root@localhost:/data/local/tmp/QDC_files/hqc_fastrpc_intrinsic/
```

Run on the board:

```sh
cd /data/local/tmp/QDC_files/hqc_fastrpc_intrinsic
chmod +x hqc_host
export ADSP_LIBRARY_PATH="$PWD;/usr/lib/dsp/cdsp;/dsp/cdsp"
export LD_LIBRARY_PATH="$PWD:/usr/lib"
./hqc_host
```

Optional iteration count:

```sh
./hqc_host 10000
```

Expected output includes `result=PASS` plus elapsed time and average time per
decode.

Verified on QDC RB3 Gen 2:

```text
./hqc_host 1000
result=PASS
total_decodes=16000
us_per_decode=230.258
```
