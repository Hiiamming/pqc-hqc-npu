#!/system/bin/sh
set -eu
cd /data/local/tmp/QDC_files/hqc_whole/hqc256_npu_fastest_nonct && chmod +x hqc_host && export ADSP_LIBRARY_PATH="$PWD;/vendor/lib/rfsa/adsp;/vendor/lib/rfsa/cdsp;/dsp" && export LD_LIBRARY_PATH="$PWD:/vendor/lib64:/system/lib64:/apex/com.android.runtime/lib64/bionic" && ./hqc_host 10000
