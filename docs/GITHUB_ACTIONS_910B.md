# GitHub Actions: ARM64 CANN 9.0 experimental builds

## Workflows

`.github/workflows/main.yml` has these phases:

1. **PR / main push:** `host-checks` runs Python profile tests, compiles and executes pure-host C++ hit epoch decoder tests, tests the production scan pipeline with a mocked NPU API, tests per-thread context binding with a mocked ACL runtime, and checks `make -n` macro propagation. This phase does **not** compile `.asc` or exercise an NPU.
2. **Actions -> Run workflow (manual):** `plan-build` validates inputs, then `build-arm64` compiles `kernel`, `proof`, and both miners. It uses `ubuntu-22.04-arm` by default, and installs CANN 9.0.0. No NPU device is available on a standard GitHub-hosted runner.
3. **Optional pre-release:** `publish_release=true` publishes experimental build artifacts only when *all selected compilation jobs succeed*. Default is `false`, leaving artifacts under the Actions run for 14 days.

## Build profiles

| Profile | MBATCH | BUFS | BACKOFF | BASEN | HIT_EPOCH | Purpose |
|---|---:|---:|---:|---:|---:|---|
| `baseline` | 1 | 4 | 2000 | 0 | **0** | Explicit opt-in reference with the original kernel/hit protocol |
| `mbatch8_legacy` | 8 | 4 | 2000 | 128 | **0** | Matched A/B control retaining per-strip memset |
| `mbatch8` (default) | 8 | 4 | 2000 | 128 | **1** | Eight strips per batch with per-core hit epochs |
| `matrix` | — | — | — | — | — | Independently compile all three profiles |
| `custom` | 8 | 4 | 2000 | 128 | **1** | Defaults shown; each custom input can be overridden |

All presets target `RANK=128`, `VEC2X=2`, `BUFS=4`, `BACKOFF=2000`.

Both the workflow selection and `scripts/ci_profiles.py` default to `mbatch8`. Custom inputs also default to `MBATCH=8`, `BUFS=4`, `VEC2X=2`, `BACKOFF=2000`, `HIT_EPOCH=1`. The `baseline` profile deliberately keeps MBATCH=1 for comparison; select it explicitly only when needed. Local Makefile/CMake defaults use the same five values. The existing `BASEN` choice is unchanged: local `0`, CI mbatch8/custom `128`; pass `BASEN=128` locally for a matched build. A-side input always uses the natural row-major layout with direct H2D upload.

**Compare `mbatch8_legacy` and `mbatch8` first.** They differ **only** in `HIT_EPOCH`, isolating the observed `aclrtMemsetAsync` long-tail hypothesis. `baseline` differs in multiple tiling parameters and cannot isolate the change.

The `HIT_EPOCH=1` result is one 512-byte-padded record per Vector core. It copies more result bytes to the host per super-strip than the old 8-byte result, trading small extra D2H bandwidth for the removal of per-strip device memset and cross-core false sharing. Performance gain is **not guaranteed**. Epochs persist across scan resets; the host checks epoch and strip bounds before accepting a hit.

Input validation allows `MBATCH=1..8`, `BUFS=1..16`, `VEC2X=1..4` with `BUFS % VEC2X == 0`, `BACKOFF=0..100000`, `BASEN=0` or a 32-aligned tile width up to `128*MBATCH`, `HIT_EPOCH=0/1`. This does not validate hardware support.

`RANK` is fixed at **128** in this workflow because the device kernel uses that geometry. `NINNER` and `NFOLD` remain governed by the Makefile and the protocol.

## Artifacts

Each successful build produces its own `ascend_prl_910b_<profile>_<run>_<attempt>.tar.gz` with compiled binaries, `.sha256` checksum, and `.manifest.json` containing the profile, source SHA, external dependency commits, and an explicit `runtime_npu_verification: NOT_RUN` status.

Build dependencies `pearl` and BLAKE3 are fetched from their upstream repositories at build time, as in the original workflow. Their resolved revisions are captured in the manifest for traceability, but they are not pinned: future builds may be affected by upstream changes. For strict reproducibility, pin known-good commits before promoting the workflow to release CI.

## Configuration and limitations

- Default runner: GitHub-hosted `ubuntu-22.04-arm`. CANN 9.0.0 Toolkit installation requires considerable free space; if the hosted runner has insufficient space, provision a large ARM64 runner and set repository variable `ASCEND_BUILD_RUNNER` to its label. It must be capable of running this workflow's privileged (`sudo`) installation steps.
- CANN toolkit URL defaults to Huawei's CANN 9.0.0 AArch64 installer; override with repository variable `CANN_TOOLKIT_URL` if your approved mirror is different. Link accessibility and installer behavior must be checked in a real workflow run.
- The proof crate uses absolute `/root/pearl-rust` dependencies, so the build job explicitly installs Pearl and Rust for root. If that dependency layout changes, update `proof-ffi/Cargo.toml` and the workflow together.
- This workflow checks **host-level logic and compilation** only. No GitHub-hosted NPU is provisioned; no bitwise reference transcript verification or real TH/s profiling is claimed. It cannot certify `MBATCH>1`, synchronization correctness, or kernel throughput.
- A genuine end-to-end gate requires a driver-configured Ascend 910B self-hosted runner, deterministic challenge fixtures and verification against the reference `zk_pow` verifier. Do not promote experimental binaries based on a green build alone.

## Local preflight

```bash
python3 -m unittest discover -s tests -p 'test_ci_*.py' -v
c++ -O2 -std=c++17 -Wall -Wextra -Werror -pedantic -o /tmp/test_hit_epoch tests/test_hit_epoch.cpp && /tmp/test_hit_epoch
cc -O2 -std=c11 -Wall -Wextra -Werror -o /tmp/test_scan_pipeline tests/test_scan_pipeline.c -lpthread && /tmp/test_scan_pipeline
c++ -O2 -std=c++17 -Wall -Wextra -Werror -pedantic -pthread -Itests/stubs -o /tmp/test_host_context tests/test_host_context.cpp && /tmp/test_host_context
python3 scripts/ci_profiles.py  # profile=mbatch8 by default
INPUT_PROFILE=matrix python3 scripts/ci_profiles.py
make -n kernel  # checks local defaults
make -n kernel RANK=128 MBATCH=8 BUFS=4 VEC2X=2 BACKOFF=2000 BASEN=128 HIT_EPOCH=1
```
