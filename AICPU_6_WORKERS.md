# PRL keyed BLAKE3 — six AICPU workers

The PRL keyed 64-byte BLAKE3 kernel now launches on **six independent AICPU streams** per strip. Worker `w` processes bands `w, w+6, w+12, ...` and has its own result buffer and completion event. The original NEON four-lane keyed BLAKE3 compression and vector-side exclusion mask are preserved. All six worker results are synchronized before the pipeline slot is recycled.

Enable with `PRL_AICPU_PERCENT=...` (1–100); 0 disables AICPU as before. Build using the repository's normal `scripts/build.sh` on a supported Ascend CANN system.

**Important:** six streams are not a guarantee of six physical AICPU cores or a speedup. If `nbands < 6`, some workers have no bands. The synthetic unkeyed benchmark's 47 Mhash/s cannot be equated to PRL keyed end-to-end throughput. No Ascend hardware/CANN compiler was available for validation here; run correctness checks against the original miner and compare total strip/s. This change does not replace keyed PRL hashes with unkeyed benchmark hashes.
