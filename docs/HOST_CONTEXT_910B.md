# Host context and default configuration update

## Changes

- The supplied profiler export records 2,248 `aclrtSetDevice` calls totalling
  267,774.62 us (119.117 us average). These are summed host API durations,
  not an end-to-end speedup estimate.
- `pearl_init` selects the device once, checks the result, and creates one
  explicit ACL context before allocating streams and events. Context creation
  makes it current on the initialization thread, so that thread's TLS cache
  is seeded without an extra runtime call.
- B upload, strip submission and key/target upload use a per-thread context
  cache. A prep thread calls `aclrtSetCurrentContext` on its first use only;
  subsequent calls on the same thread perform a TLS pointer comparison.
  Binding failure aborts before device work is submitted. No hot-path
  `aclrtSetDevice`, `aclrtGetCurrentContext`, or `aclrtGetDevice` is introduced.
- The existing miner creates a new prep thread per iteration. Each such thread
  still needs one context bind; this change does not add a persistent worker.
- After all scans and prep threads have drained, `pearl_finalize` frees resources
  and destroys the owned explicit context. The miner owns the ACL context on
  its calling threads; external code must not switch it behind the TLS cache.
  The existing API remains a single-initialization, single-device instance.
- Makefile, CMake and kernel fallbacks already had the requested defaults:
  `BUFS=4 VEC2X=2 BACKOFF=500 MBATCH=1 HIT_EPOCH=1`.
  GitHub Actions custom inputs, Python fallback inputs, and the default
  `mbatch1` preset now match. The `mbatch8` and `mbatch8_legacy`
  profiles remain available for explicit comparisons; `baseline` remains an explicit comparison profile.
- `BASEN` was not requested and is unchanged (local default 0, CI mbatch1/mbatch8/custom
  128). The workflow checks both the local defaults and explicit overrides.

## Validation

The workflow runs pure-host tests for the actual context cache (independent
threads, repeated submissions without extra runtime calls, failed-bind retry,
cache invalidation), profile CLI defaults, matched epoch comparison, epoch
decoding and scan pipeline draining. These tests do not execute Ascend kernels.

CANN compilation, NPU proof verification and a new profiler capture must run
on the target environment. After warmup, the expected `aclrtSetDevice` count
is one per miner initialization; `aclrtSetCurrentContext` occurs once per prep
thread that uploads B. Measure verified shares and sustained throughput before
claiming a performance improvement.

## API basis

CANN 9.0 documents that threads need an associated context, streams/events
belong to that context, and explicit contexts support cross-thread association.
It also restricts context operations on the default context, so this change
creates an explicit context instead of capturing the implicit default context:

https://www.hiascend.com/doc_center/source/zh/canncommercial/900/programug/acldevg/runtime_doc_dev_0001.html
