#!/usr/bin/env bash
# Same arguments as the selected miner binary. Never embeds pool credentials.
set -euo pipefail
cd "$(dirname "$0")/.."
if (( $# < 7 )); then
  echo "Usage: $0 ./build/ascend_prl_kryptex dev worker host port wallet password" >&2
  exit 1
fi
mkdir -p timing_logs
stamp=$(date +%Y%m%d_%H%M%S)_$$
export PRL_TIMING="${PRL_TIMING:-1}"
export PRL_TIMING_PRINT="${PRL_TIMING_PRINT:-1}"
export PRL_TIMING_DEVICE="${PRL_TIMING_DEVICE:-1}"
export PRL_TIMING_SUMMARY_EVERY="${PRL_TIMING_SUMMARY_EVERY:-10}"
export PRL_TIMING_CSV="${PRL_TIMING_CSV:-$PWD/timing_logs/$stamp.csv}"
echo "Timing CSV: $PRL_TIMING_CSV" >&2
"$@" 2>&1 | tee "timing_logs/$stamp.log"
