#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
test_tmp=$(mktemp -d)
trap 'rm -rf "$test_tmp"' EXIT
cc -std=gnu11 -Wall -Wextra -Werror -pthread -Isrc \
  tests/scan_mock.c src/scan_strip_async.c src/prl_timing.c \
  src/proofgz.c tests/original_proofgz.c -lz -o "$test_tmp/scan"
PRL_TIMING_PRINT=0 PRL_TIMING_CSV="$test_tmp/scan.csv" "$test_tmp/scan" 2>"$test_tmp/scan.log"
PRL_TIMING=0 "$test_tmp/scan"
cc -std=gnu11 -Wall -Wextra -Werror -pthread -Isrc \
  tests/logger_threads.c src/prl_timing.c -o "$test_tmp/logger"
PRL_TIMING_PRINT=0 PRL_TIMING_CSV="$test_tmp/thread.csv" "$test_tmp/logger" 2>"$test_tmp/thread.log"
python3 - "$test_tmp" <<'PY'
import csv,sys
from pathlib import Path
p=Path(sys.argv[1])
rows=list(csv.DictReader((p/'thread.csv').open()))
assert len(rows)==400
for r in range(4):
    subset=[x for x in rows if int(x['round'])==r]
    assert len(subset)==100 and all(int(x['slot'])==r and int(x['strip'])==3 for x in subset)
rows=list(csv.DictReader((p/'scan.csv').open()))
assert any(r['metric']=='scan.hit_drain' for r in rows)
assert all(float(r['ms'])>=0 for r in rows)
PY
python3 scripts/timing_summary.py --skip-rounds 0 "$test_tmp/thread.csv" > "$test_tmp/summary"
grep -q test.concurrent "$test_tmp/summary"
# The production shared library builds this same source as C++.
c++ -std=c++11 -Wall -Wextra -Werror -pthread -Isrc -c kernel/prl_timing.cpp -o "$test_tmp/timing.o"
cc -std=gnu11 -fopenmp -Wall -Wextra -fsyntax-only -Isrc src/miner.c
bash -n scripts/run_timing.sh
printf 'PASS: scan order/drain, encoding byte equivalence, disabled mode, concurrent CSV/context, summary, C/C++ logger builds, miner syntax.\n'
