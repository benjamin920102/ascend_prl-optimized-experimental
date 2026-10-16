# ascend_prl / 910B4: SGLang-inspired optimization review

**Status:** Experimental source patch. Code reviewed against the provided Oct 2026 snapshots, but not compiled or profiled on Ascend 910B4 / CANN 9.0 in this environment. Do not mine shares until the output passes reference proof verification. All mining sessions use the configured wallet.

## 1. The mathematical constraint

At RANK=128, K=4096, the algorithm uses `NFOLD=32`, `NINNER=1`, and a 128-wide K-step. The vector consumer needs **every prefix sum**: after each `p`, it does `acc += partial[p]`, an XOR reduction of `acc`, and an XOR/rotation update of the corresponding transcript word. Combining 32 partial matrices in L0C and exporting only the final sum does **not** preserve the protocol transcript.

Likewise, arbitrary `int32 -> int16/int8` conversion loses information. Even a single signed int8 dot-product of length 128 can exceed the range of signed 16-bit values. Do not use quantization, changed NFOLD, or grouping in K without checking against `zk_pow`'s canonical verifier and proving equivalence.

## 2. What the actual supplied sources do

- `kernel/pearl_hlc2_cube.asc`: one `IterateAll(partG[Slot(lc,buf,p)])` per fold, one int32 partial per 64x128 output, for every band and strip.
- `kernel/pearl_hlc2_vec.asc`: 32 partial reads and prefix folds, then per-tile scalar BLAKE3.
- `kernel/pearl_hlc2_cube.asc`: uploads the natural `[MBATCH*R,K]` A layout directly and uses a transposed Matmul view; there is no host-side A repacking stage.
- `kernel/CMakeLists.txt`: has BACKOFF, but top-level `Makefile` previously hardcoded `BUFS=4`, `VEC2X=2` and did not pass `BACKOFF` or `BASEN`.
- `src/prep.c::compute_bn_pack`: packs BT by `[band][fold][column][k]`; changing the fold grouping requires changing this packing and the proof definition, too.
- `sglang-main/docs/docs/hardware-platforms/ascend-npus/development/operator_performance_optimizing.mdx`: recommends alignment, data reuse, double buffering and profiling. Actual Ascend C kernels are primarily in external `sgl-kernel-npu`, not in the supplied `sglang-main.zip`.

## 3. Quantitative baseline (not measured throughput)

Per `band x logical strip` at RANK=128, MBATCH=1:

| Item | Count | Bytes |
| --- | ---: | ---: |
| One int32 partial | 64x128 | 32 KiB |
| Cube partial writes, 32 folds | 32x | 1 MiB |
| Vector partial reads, 32 folds | 32x | 1 MiB |
| A operand input over all folds (no cache accounting) | 128x4096 int8 | 512 KiB |
| BT operand input over all folds (no cache accounting) | 64x4096 int8 | 256 KiB |
| Allocated partial-ring capacity at BUFS=4 / MBATCH=1 | 20x4x32x32 KiB | 80 MiB |

`MBATCH=2/4/8` amortizes BT reuse across strips but does **not** reduce 1 MiB partial write/read per *logical* strip. `BUFS=2` reduces partial-ring capacity to 40 MiB but may cause producer back-pressure.

## 4. Changes included in this experiment

1. Expose `BUFS`, `VEC2X`, `BACKOFF` and `BASEN` from Makefile to the kernel CMake compilation.
2. For `MBATCH=1`, replace zero-stride 2D DMA with an equivalent contiguous 32 KiB read.
3. For `MBATCH>1`, reserve 4 KiB per lane for transcripts in vector UB. Compute all lanes' original folds first, finish partial GM reads, release the Cube slot, **then** hash each saved transcript. Original folding order, 32-bit arithmetic and transcript layout are kept.
4. Add compile-time checks for UB footprint, slot scheduling divisibility and `K == NFOLD*NINNER*128`. RANK=128 explicitly requires `NFOLD=32,NINNER=1`.
5. Use `MBATCH=1` by default in Makefile, CMake, both kernel fallbacks, and GitHub Actions. A inputs upload directly in their natural layout without an extra staging allocation or CPU transpose.

**This patch does not reduce the total Fixpipe bytes**. It focuses on overlap and scheduling and exposes safe tiling experiments. Performance gains are hypotheses, not measured results.

## 5. Measurement matrix

Use the same board, thermal/power environment, pool difficulty, matrix dimensions, number of warmup rounds, and reference verification path.

```sh
# From project root, within the official CANN 9.0 environment.
make kernel RANK=128 MBATCH=1 BUFS=4 VEC2X=2 BACKOFF=2000 BASEN=0
make kernel RANK=128 MBATCH=1 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=0
make kernel RANK=128 MBATCH=2 BUFS=4 VEC2X=2 BACKOFF=2000 BASEN=128
make kernel RANK=128 MBATCH=2 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=128
make kernel RANK=128 MBATCH=4 BUFS=4 VEC2X=2 BACKOFF=2000 BASEN=128
make kernel RANK=128 MBATCH=8 BUFS=4 VEC2X=2 BACKOFF=2000 BASEN=0
make kernel RANK=128 MBATCH=8 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=128
```

Build **complete binaries** with matching `make RANK=128 MBATCH=N ...` and test each independently. Changing only the `.so` while keeping incompatible proof/host binaries is unsafe. Ensure the real benchmark uses normal folding (do not set `PRL_NOFOLD`). Capture sustained verified logical TH/s, NPU-side timings, host preparation/H2D time, Cube/Vector kernel occupancy and back-pressure. Monitor the absolute `aic_fixpipe_time` and `aiv_mte2_time` as well as their ratios; overlapping pipeline percentages need not total 100%.

### Correctness gates

- Compare MBATCH=1 and MBATCH=2/4/8 using **identical** logical A/B matrices. The resulting 16-word transcript for each `(strip, band, tile)` must be bit-for-bit identical.
- Run `zk_pow` reference proof verification for each accepted hit; do not rely only on GPU-side BLAKE3 comparisons.
- Include `n_hi` values >1, last band, ring wraparound, at least 3 in-flight strips, repeated workload changes, and cases where B buffer flips while A is in flight.
- Verify no producer/consumer stalls or hangs under `BUFS=2`; any dropped share/incorrect proof is a blocker.
- Record 5+ stable trials and compare median **valid** TH/s, not only percentage ratios.

## 6. Highest-potential NEXT experiments, not implemented

### A. Fold-stage streaming ring (likely working-set / overlap lever)

Keep `NFOLD=32` and output each `partial[p]` losslessly, but replace `[BUFS][NFOLD][64][128]` whole-band ring with a `[BUFS][STAGES][64][128]` window (STAGES=2 or 4). Publish/acknowledge each fold stage, never overwrite before the consumer's MTE2 completed. Let Vector fold the stages in the exact p order. This reduces **ring storage** (up to 8–16x), not per-strip write/read bytes. A mixed AIC/AIV launch with supported cross-core flags may cut per-stage synchronization overhead. CANN high-level Matmul internally uses some cross-core flag resources, so consult the exact CANN 9.0 build and avoid flag ID conflicts/deadlock.

### B. Vector UB prefetch / double-buffer (likely AIV MTE2 stall lever)

Allocate two 32 KiB partial buffers. While consuming `partial[p]`, issue MTE2 for `partial[p+1]` into the other buffer, with correct `MTE2_V` and `V_MTE2` dependencies before either read or buffer reuse. The present `PipeBarrier<PIPE_ALL>` after every `Add` prevents overwrite hazards but also inhibits overlap. Implement only with actual-device tests. UB footprint should remain below hardware limits.

### C. Avoid premature L0C accumulation / lossy int16 partials

L0C accumulation in K is only valid **within each canonical fold**, not across the 32 fold steps. `int16` alone is insufficient for exact dot products. A custom lossless representation would require proven range, packing cost model and a path that genuinely shrinks Fixpipe output traffic.

## References

- User-provided `sglang-main.zip` NPU operator development and performance guides.
- Ascend official 2201 architecture: https://www.hiascend.com/document/detail/en/CANNCommunityEdition/910/programug/Ascendcopdevg/docs/en/guide/programming_guide/advanced_programming/hardware_implementation/architecture_spec/npu_architecture_version_2201.md
- Ascend official cross-core flags and caution with Matmul advanced API: https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/900beta2/API/ascendcopapi/atlasascendc_api_07_0206.html
- SGLang NPU kernel repository: https://github.com/sgl-project/sgl-kernel-npu

## GitHub Actions

`main.yml` now supports validated profile selection, independent ARM64+CANN9 builds, 14-day workflow artifacts, and opt-in explicitly unverified pre-releases. See [GITHUB_ACTIONS_910B.md](GITHUB_ACTIONS_910B.md).
