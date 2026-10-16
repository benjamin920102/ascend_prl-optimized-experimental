# GitHub Actions builds for Ascend 910B

Every kernel submission handles exactly one logical strip. There is no batch-size input, build macro, runtime getter, or manifest field.

## Profiles

| Profile | BUFS | VEC2X | BACKOFF | BASEN | HIT_EPOCH |
| --- | ---: | ---: | ---: | ---: | ---: |
| default | 4 | 2 | 100 | 128 | 1 |
| baseline | 4 | 2 | 100 | 0 | 0 |
| legacy | 4 | 2 | 100 | 128 | 0 |
| epoch | 4 | 2 | 100 | 128 | 1 |
| custom (defaults) | 4 | 2 | 100 | 128 | 1 |

`matrix` builds baseline, legacy and epoch separately. Compare legacy and epoch for matched result-protocol tests. All profiles target RANK=128.

## Build and download

Upload the source to your repository, open Actions, select Run workflow and use the default profile. ARM64 builds install CANN 9.0.0, fetch the external proof and BLAKE3 sources, then compile the kernel, proof library and both miners. Source revisions are recorded.

Download the run artifact and extract the compiled tar.gz into build/. Rebuild and deploy the kernel and miner together. Publishing a pre-release remains opt-in.

Custom inputs allow BUFS=1..16, VEC2X=1..4 (BUFS must be divisible by VEC2X), BACKOFF=0..100000, BASEN=0 or a 32-aligned width no greater than 128, and HIT_EPOCH=0/1. This validation does not certify hardware support.

Local Makefile/CMake defaults use BUFS=4, VEC2X=2, BACKOFF=100 and HIT_EPOCH=1. BASEN defaults to 0 locally, which selects the full 128-wide tile; CI uses explicit 128.

## Verification

Host checks exercise profile selection, epoch decoding, early-hit draining, ACL context caching and Makefile argument propagation. GitHub compile artifacts have not been validated on an NPU. Actual proof verification and throughput measurements require the target board.

```bash
python3 -m unittest discover -s tests -p 'test_ci_*.py' -v
python3 scripts/ci_profiles.py
make -n kernel RANK=128 BUFS=4 VEC2X=2 BACKOFF=100 BASEN=128 HIT_EPOCH=1
```
