#!/usr/bin/env bash
# Runs as root because proof-ffi/Cargo.toml currently requires /root/pearl-rust.
set -euo pipefail
: "${CI_MBATCH:?missing CI_MBATCH}"
: "${CI_BUFS:?missing CI_BUFS}"
: "${CI_VEC2X:?missing CI_VEC2X}"
: "${CI_BACKOFF:?missing CI_BACKOFF}"
: "${CI_BASEN:?missing CI_BASEN}"
: "${CI_AT_PACK:?missing CI_AT_PACK}"
: "${CI_HIT_EPOCH:?missing CI_HIT_EPOCH}"
: "${CI_BLK:?missing CI_BLK}"
: "${GITHUB_WORKSPACE:?missing GITHUB_WORKSPACE}"
if [[ "$(uname -m)" != "aarch64" ]]; then
  echo "ERROR: Ascend CANN Linux-aarch64 build requires an ARM64 runner" >&2
  exit 1
fi
if [[ ! -f /usr/local/Ascend/cann/set_env.sh ]]; then
  echo "ERROR: CANN environment missing: /usr/local/Ascend/cann/set_env.sh" >&2
  exit 1
fi
# The installed CANN 9.0.0 path may be cann or cann-9.0.0, with an arch-specific subdir.
source /usr/local/Ascend/cann/set_env.sh
export PATH="/root/.cargo/bin:$PATH"
if [[ -z "${ASC_MODULES:-}" ]]; then
  for modules in \
    /usr/local/Ascend/cann/aarch64-linux/tikcpp/ascendc_kernel_cmake/asc_modules \
    /usr/local/Ascend/cann-9.0.0/aarch64-linux/tikcpp/ascendc_kernel_cmake/asc_modules; do
    if [[ -d "$modules" ]]; then
      export ASC_MODULES="$modules"
      break
    fi
  done
fi
if [[ ! -d "${ASC_MODULES:-}" ]]; then
  echo "ERROR: AscendC CMake modules not found; check CANN 9.0.0 toolkit installation" >&2
  exit 1
fi
cd "$GITHUB_WORKSPACE"
echo "=== Build configuration (not hardware verified) ==="
echo "RANK=128 MBATCH=$CI_MBATCH BUFS=$CI_BUFS VEC2X=$CI_VEC2X BACKOFF=$CI_BACKOFF BASEN=$CI_BASEN AT_PACK=$CI_AT_PACK HIT_EPOCH=$CI_HIT_EPOCH"
cargo --version
make \
  RANK=128 MBATCH="$CI_MBATCH" BUFS="$CI_BUFS" VEC2X="$CI_VEC2X" \
  BACKOFF="$CI_BACKOFF" BASEN="$CI_BASEN" AT_PACK="$CI_AT_PACK" HIT_EPOCH="$CI_HIT_EPOCH" \
  BLK="$CI_BLK" all
for file in build/libpearl_hlc2.so build/libpearl_proof.so build/ascend_prl_kryptex build/ascend_prl_k1; do
  test -s "$file" || { echo "ERROR: $file missing or empty" >&2; exit 1; }
done
