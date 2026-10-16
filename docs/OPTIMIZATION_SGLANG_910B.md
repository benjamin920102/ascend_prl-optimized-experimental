# Ascend 910B optimization notes

## Fixed execution geometry

Each submission processes one logical strip. The Cube computes [64,128] int32 partials, and Vector reads each partial with a contiguous 32 KiB copy. Multi-strip branches, strided lane copies and batch-size controls have been removed. This specializes the previous one-strip path; it does not claim a measured speedup.

RANK=128 requires NINNER=1, NFOLD=32 and K=4096. Every prefix sum participates in the canonical transcript. Combining folds or truncating int32 partials changes that transcript and is not an equivalent optimization.

## Current defaults

BUFS=4, VEC2X=2, BACKOFF=100, HIT_EPOCH=1. The 20 Cube producers have 40 Vector consumer blocks. Each vector block keeps one 4 KiB transcript in UB and releases its producer slot before scalar hashing.

Per band and strip, 32 folds write 1 MiB of partials and read 1 MiB back. The partial ring at BUFS=4 occupies 80 MiB. BUFS=2 halves storage but can increase producer back-pressure.

## Remaining experiments

- Compare BACKOFF=100/500/2000 while holding all other settings fixed.
- Compare BUFS=2/4/8 using identical logical work.
- Explore two UB partial buffers with explicit MTE2/Vector dependencies to overlap reading the next fold with processing the current fold. Do not remove barriers without replacing their dependencies.
- Measure host preparation, proof generation, compression and submission against NPU timeline gaps.
- Consider specialized or vectorized BLAKE3 after separating hashing from polling/control costs.

## Correctness and measurement

Compare transcript words bit for bit against the reference, verify generated proofs, and exercise ring wraparound, stale epochs, repeated B changes and early-hit draining. Host tests do not execute NPU kernels.

Use actual completed strips per second for fair throughput measurements. The current displayed estimate charges a full scan even when a hit stops it early, so it can overestimate completed work. PRL_NOFOLD is diagnostic only and does not produce valid mining transcripts.
