# Experimental Vector + AICPU BLAKE3 split (910C)

**Status: source-level experimental prototype; not device-compiled or tested. Do not use for real mining without end-to-end verification.**

## Strategy

* Cube still performs INT8 matmul and uses its original producer/consumer flags.
* Vector folds every band; it hashes only tiles not assigned to AICPU.
* Vector exports each transcript band and publishes a monotonically increasing ready flag.
* Separate AICPU stream executes NEON x4 keyed-BLAKE3 on its assigned tiles, checking the same 256-bit target.
* The AICPU kernel is enqueued after Vector but runs concurrently, waiting on each **band** (not the whole Vector kernel). Host joins the two result streams before considering a no-hit result.
* Offloading is controlled by `PRL_AICPU_PERCENT` (0-100), initial suggested 25.

## Build

Inside the existing CANN 9 / dav-2201 environment:

```sh
source /usr/local/Ascend/cann-9.0.0/set_env.sh
make RANK=128 kernel
make RANK=128 proof miner
PRL_AICPU_PERCENT=25 ./build/ascend_prl_kryptex [your existing arguments]
```

The CMake definition invokes `bisheng` on `kernel/pearl_aicpu_blake3.aicpu` and links the result into the kernel .so. Toolchain support for AICPU device linkage inside an ASC shared library remains **unverified**. If it fails, collect the exact compiler/linker error rather than removing the error check.

`PRL_AICPU_PERCENT=0` selects original Vector-only path in the modified binary; unset has the same effect.

## Essential correctness concerns to verify on actual hardware

1. **GM coherency and flag ordering:** Vector `DataCopy` + barrier + published GM flag must make the complete transcript visible to AICPU. This is hardware/toolchain dependent. If the AICPU reads stale lines, use a supported cross-engine event or cache-sync mechanism. This prototype's `volatile`/atomic loads alone are not a proof of NPU/AICPU coherence.
2. AICPU forward progress on all bands; `aclrtSynchronizeEventWithTimeout` must report completion. An unreleased flag can cause a spin/hang.
3. Compare keyed hashes for deterministic 64-byte messages with a trusted host BLAKE3 keyed reference. BLAKE3 flags are `0x1b` (KEYED_HASH | CHUNK_START | CHUNK_END | ROOT), little-endian words.
4. Validate `PRL_AICPU_PERCENT=0,25,50,75,100` and compare all accepted solutions and transcript data with baseline. Confirm partition (`band*nhi+hi)%100` exactly covers all tiles without duplication or omission.
5. Validate pipelining with `PEARL_PIPE_DEPTH=2,4`, key changes, multiple strips, and no-hit/hit targets. Profile cube wait time, end-to-end throughput, AICPU kernel and memory traffic.

## Caveats

* This is **not** a validated performance improvement. AICPU stream may be slower than its assigned workload. Its single kernel currently uses one 4-lane batch at a time; optimizing across multiple AICPU kernels/streams is future work.
* Diagnostic transcript export remains valid. The production path now exports transcripts when offloading is enabled, increasing GM write traffic.
* No Ascend toolchain or 910C was available in the preparation environment. Source packaging and static checks are possible here, but hardware correctness is not established.
* Keep the original ZIP as rollback and compare original Vector-only outputs before experimenting.
