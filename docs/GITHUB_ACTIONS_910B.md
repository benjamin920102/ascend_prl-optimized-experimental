# GitHub Actions: ARM64 CANN 9.0 experimental builds

## Workflows

`.github/workflows/main.yml` has these phases:

1. **PR / main push:** `host-checks` runs a Python layout sanity test, CI profile validation tests, and `make -n` macro expansion. This phase does **not** compile `.asc` or exercise an NPU.
2. **Actions -> Run workflow (manual):** `plan-build` validates inputs, then `build-arm64` compiles `kernel`, `proof`, and both miners. It uses `ubuntu-22.04-arm` by default, and installs CANN 9.0.0. No NPU device is available on a standard GitHub-hosted runner.
3. **Optional pre-release:** `publish_release=true` publishes experimental build artifacts only when *all selected compilation jobs succeed*. Default is `false`, leaving artifacts under the Actions run for 14 days.

## Build profiles

| Name | RANK | MBATCH | BUFS | VEC2X | BACKOFF | BASEN | AT_PACK | Role |
|---|---:|---:|---:|---:|---:|---:|---:|---|
| baseline | 128 | 1 | 4 | 2 | 2000 | 0 | 0 | Reference build |
| mbatch2 | 128 | 2 | 2 | 2 | 500 | 128 | 0 | Early release and tuned tiling experiment |
| atpack | 128 | 2 | 2 | 2 | 500 | 128 | 1 | Experimental A-side repack |
| matrix | 128 | Each of the above, built independently | | | | | | Matrix comparison |
| custom | 128 | Manual inputs | | | | | | Alternative tiling experiment |

Compile and test configurations **separately**. Do not swap in just `libpearl_hlc2.so` from an incompatible build.

Input validation allows `MBATCH=1..8`, `BUFS=1..16`, `VEC2X=1..4` with `BUFS % VEC2X == 0`, `BACKOFF=0..100000`, `BASEN=0` or 32-byte-aligned tile width up to `128*MBATCH`, and `AT_PACK=0/1`. This rejects invalid **configuration shapes**, not hardware unsupported tiling. Hardware support remains unverified.

`RANK` is fixed at **128** in this workflow because the kernel's `R` constant and the supplied optimization focus on that exact geometry. `NINNER` and `NFOLD` remain governed by the Makefile and the protocol.

## Artifacts

Each successful build produces its own `ascend_prl_910b_<profile>_<run>_<attempt>.tar.gz` with compiled binaries, `.sha256` checksum, and `.manifest.json` containing the profile, source SHA, external dependency commits, and an explicit `runtime_npu_verification: NOT_RUN` status.

Build dependencies `pearl` and BLAKE3 are fetched from their upstream repositories at build time, as in the original workflow. Their resolved revisions are captured in the manifest for traceability, but they are not pinned: future builds may be affected by upstream changes. For strict reproducibility, pin known-good commits before promoting the workflow to release CI.

## Configuration and limitations

- Default runner: GitHub-hosted `ubuntu-22.04-arm`. CANN 9.0.0 Toolkit installation requires considerable free space; if the hosted runner has insufficient space, provision a large ARM64 runner and set repository variable `ASCEND_BUILD_RUNNER` to its label. It must be capable of running this workflow's privileged (`sudo`) installation steps.
- CANN toolkit URL defaults to Huawei's CANN 9.0.0 AArch64 installer; override with repository variable `CANN_TOOLKIT_URL` if your approved mirror is different. Link accessibility and installer behavior must be checked in a real workflow run.
- The proof crate uses absolute `/root/pearl-rust` dependencies, so the build job explicitly installs Pearl and Rust for root. If that dependency layout changes, update `proof-ffi/Cargo.toml` and the workflow together.
- This workflow checks **compilation** only. No GitHub-hosted NPU is provisioned; no bitwise reference transcript verification or real TH/s profiling is claimed. It cannot certify `AT_PACK=1`, `MBATCH>1`, synchronization correctness, or kernel throughput.
- A genuine end-to-end gate requires a driver-configured Ascend 910B self-hosted runner, deterministic challenge fixtures and verification against the reference `zk_pow` verifier. Do not promote experimental binaries based on a green build alone.

## Local preflight

```bash
python3 tests/test_repack_layout.py
python3 -m unittest discover -s tests -p 'test_ci_*.py' -v
python3 scripts/ci_profiles.py  # profile=mbatch2 by default
INPUT_PROFILE=matrix python3 scripts/ci_profiles.py
make -n kernel RANK=128 MBATCH=2 BUFS=2 VEC2X=2 BACKOFF=500 BASEN=128 AT_PACK=1
```
