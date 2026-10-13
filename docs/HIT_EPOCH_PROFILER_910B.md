# 910B profiler optimization v3 — Hit epoch + GitHub Actions

## Data and scope

The previous `export.zip` study found sporadic >10ms host `aclrtMemsetAsync` calls between Cube and Vector launch; long Cube scalar tails correlate with delayed Vector launch. Correlation is not proof of causation. The current `scan_full` log is ~116ms per iteration. We change **only result publication protocol and early-hit drain**, not the PoW transcript arithmetic.

## Changes

1. Add `HIT_EPOCH=1` (default). Per super-strip, host generates a monotonically increasing **64-bit epoch**, never reset by `pearl_use_b`. No per-strip `aclrtMemsetAsync` between Cube and Vector launch.
2. Vector workers publish valid encoded tiles in independent 512-byte records in each in-flight slot, each record containing `[encoded, epoch low, epoch high]`. Different workers cannot race on the same result words/cacheline. A worker may publish several valid hits and the last one wins in that worker, but all are valid candidates.
3. At init, both device record buffers are zeroed once on the Vector stream and the stream is synchronized. If a worker has no hit on a later job, its old epoch remains and is rejected.
4. Asynchronous D2H copy reads the full result array only after the Vector launch, and the already-existing completion event is recorded after the copy. Only after waiting for that event does the host scan each record for an exact epoch match and valid global-strip range. If several hits exist, choose the smallest encoded tile among valid candidates.
5. When `scan_full` observes a hit, it waits for any previously submitted next strip before returning. This avoids a reset of producer/consumer flags while old kernels are still running.
6. `HIT_EPOCH=0` retains the original per-strip memset shared-result path to enable controlled A/B benchmarking. It is not the preferred correctness path under simultaneous multi-worker hits.

## How to build and compare on 910B4

Use the **same** rank and tiling for a fair epoch-vs-legacy comparison. Keep builds in separate locations or save/copy binaries before rebuilding:

```bash
# Legacy control (all else identical)
make RANK=128 MBATCH=2 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=128 AT_PACK=0 HIT_EPOCH=0 all
# Optimized
make RANK=128 MBATCH=2 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=128 AT_PACK=0 HIT_EPOCH=1 all
```

Build outputs have the same names and must not be mixed. CI has profiles `mbatch2_legacy` and `mbatch2` that package them separately. Confirm process startup prints `HIT_EPOCH=1` for the new build.

Run both with `PRL_LOOP_PROF=1` (export it before calling the background launcher), save logs, compare `scan_full` distribution (p50/p95/p99), **verified shares**, and throughput on equivalent work. In a new `msprof` run compare Host `aclrtMemsetAsync` count/duration, Cube scalar tail, Kernel timeline overlap and device-to-host memory copies. A significant reduction in memset calls without faster sustained iter/s is possible: new result D2H is ~20KiB instead of 8B for `GRID=20,VEC2X=2`.

## Verification boundary

Host tests (CI) validate epoch decoding, slot reuse/stale detection, multiple simultaneous worker hits, high 32 epoch bits, valid strip bounds, and exact drain of early-hit submissions with a mocked backend. **They do not compile AscendC or validate NPU cache flushing, device concurrency, BLAKE3, transcript identity, or accepted proof correctness.** Those require hardware and the actual verifier. Do not treat a green GitHub build as a production guarantee.

If NPU-side validation fails, switch to `HIT_EPOCH=0` and rebuild the kernel and miners as one matching set, then capture error logs/profile.
