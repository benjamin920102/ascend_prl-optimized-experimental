#!/usr/bin/env bash
# Run on the NPU after make PROFILE=1 kernel profile-probe.
set -euo pipefail
probe="${PROBE:-./build/profile_probe}"
result_dir="${PROBE_OUT:-./profile/equivalence}"
mkdir -p "$result_dir"
for target in zero easy; do
  for wait_path in ptr copy fast; do
    for mode in 0 1 2; do
      PRL_PROF="$mode" PRL_PROF_STRIPS=0 PRL_PROF_TRACE=0 \
        PRL_PROF_OUT="$result_dir/mode${mode}_${target}_${wait_path}" \
        "$probe" "$result_dir/${target}_${wait_path}_mode${mode}.bin" \
        "${DEVICE:-0}" "${NBANDS:-83}" "${STRIPS:-6}" "${NHI:-64}" "$target" "$wait_path" 2
    done
    cmp "$result_dir/${target}_${wait_path}_mode0.bin" "$result_dir/${target}_${wait_path}_mode1.bin"
    cmp "$result_dir/${target}_${wait_path}_mode0.bin" "$result_dir/${target}_${wait_path}_mode2.bin"
  done
  cmp "$result_dir/${target}_ptr_mode0.bin" "$result_dir/${target}_copy_mode0.bin"
done
echo 'PASS: mode 0/1/2 transcripts and hits match byte-for-byte; all wait paths and scan resets checked.'
# To compare the original unmodified library as well, save its probe output and
# cmp it against *_mode0.bin with exactly the same arguments / build shape.
