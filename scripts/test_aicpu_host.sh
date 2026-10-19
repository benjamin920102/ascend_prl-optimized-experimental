#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
mode=${1:-normal}
if [[ "$mode" != normal && "$mode" != --extended ]]; then
  echo "usage: $0 [--extended]" >&2; exit 2
fi
bin=$(mktemp /tmp/pearl-aicpu-test.XXXXXX)
trap 'rm -f "$bin"' EXIT
compile() {
  "${CXX:-c++}" -std=c++14 -O2 -Wall -Wextra -pthread \
    -Itests/stubs -Ikernel tests/aicpu_parallel_test.cpp "$@" -o "$bin"
}
echo 'Normal host parity / worker participation'
compile
"$bin"
if [[ "$mode" == --extended ]]; then
  echo 'ASan + UBSan (leak detection disabled for restricted runners)'
  compile -fsanitize=address,undefined -fno-omit-frame-pointer
  ASAN_OPTIONS=detect_leaks=0 "$bin"
  echo 'pthread_create failure: all workers unavailable'
  compile tests/thread_failure.cpp -Wl,--wrap=pthread_create
  "$bin"
  echo 'pthread_create failure: partial availability'
  compile tests/thread_failure.cpp -Wl,--wrap=pthread_create -DALLOW_THREADS=3
  "$bin"
fi
