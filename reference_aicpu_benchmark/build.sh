#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${ASCEND_HOME_PATH:=/usr/local/Ascend/cann}"
command -v bisheng >/dev/null || { echo 'Cannot find bisheng; source /usr/local/Ascend/ascend-toolkit/set_env.sh'; exit 1; }
INC="$ASCEND_HOME_PATH/asc/include/aicpu_api"
[[ -f "$INC/aicpu_api.h" ]] || INC="$ASCEND_HOME_PATH/include/ascendc/aicpu_api"
[[ -f "$INC/aicpu_api.h" ]] || { echo "Cannot locate aicpu_api.h under $ASCEND_HOME_PATH"; exit 1; }
LIB="$ASCEND_HOME_PATH/lib64/device/lib64"
[[ -d "$LIB" ]] || LIB="$ASCEND_HOME_PATH/toolkit/lib64/device/lib64"
args=(-O3 blake3_bench.aicpu "--cce-aicpu-L$LIB" --cce-aicpu-laicpu_api "-I$INC")
if [[ -x "$ASCEND_HOME_PATH/toolkit/toolchain/hcc/bin/aarch64-target-linux-gnu-gcc" ]]; then
    args+=("--cce-aicpu-toolkit-path=$ASCEND_HOME_PATH/toolkit/toolchain/hcc/bin")
fi
if [[ -d "$ASCEND_HOME_PATH/toolkit/toolchain/hcc/sysroot" ]]; then
    args+=("--cce-aicpu-sysroot=$ASCEND_HOME_PATH/toolkit/toolchain/hcc/sysroot")
fi
bisheng "${args[@]}" -c -o blake3_bench.aicpu.o
bisheng --npu-arch=dav-2201 main.asc -c -o main.asc.o
bisheng blake3_bench.aicpu.o main.asc.o -o blake3_bench
./blake3_bench "$@"
