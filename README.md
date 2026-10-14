# `ascend_prl` — the world's first Ascend NPU cryptocurrency miner

> **910B profiler v3:** `HIT_EPOCH=1` is now the default: per-Vector-core epoch hit records replace per-strip device memset. `HIT_EPOCH=0` retains the original protocol for matched A/B testing. GitHub Actions includes `mbatch8_legacy` and `mbatch8`; see [`docs/HIT_EPOCH_PROFILER_910B.md`](docs/HIT_EPOCH_PROFILER_910B.md). The new protocol is **not yet NPU-verified**.

**English** | [中文](README_zh.md)

> **This source version:** All mining connections and share submissions use your configured wallet. `MBATCH=8` is the default for local builds and GitHub Actions, and A inputs upload directly in their natural layout. Upstream performance figures are not measurements of this version.

A from-scratch miner for the [Pearl](https://arxiv.org/abs/2504.09971) Proof-of-**Useful**-Work
coin on Huawei **Ascend 910B** NPUs. Tested on a 910B4; it does **~30 TH/s/device end-to-end**.

> **DISCLAMER:** for educational purpose only. Use only with hardware you are authorized to.

## Quickstart

1. **Create a Pearl wallet.** Use the [official wallet](https://github.com/pearl-research-labs/pearl/releases/tag/pearl-wallet-v2.0.0)
   and follow their instructions. You need an address like
   `prl1...YOUR_WALLET_ADDRESS`.
2. **Choose a pool.** Any standard `stratum` Pearl pool should work. These sources are tested with
   [Kryptex](https://pool.kryptex.com/prl). Note: different pools accept proofs for different M, N, K (see [Performance](#performance-and-shapes)).
3. **Build and run this version.** Extract this source on an ARM64 machine with CANN configured, enter the project directory, and install the [build dependencies](#build):
   ```bash
   make                     # defaults: RANK=128, MBATCH=8
   export WALLET=prl1...your_wallet
   export POOL=pool.host PORT=7000
   ASCEND_RT_VISIBLE_DEVICES=0,1,2,3 ./scripts/launch.sh
   ```
   Alternatively, upload this source to your GitHub repository and run **Actions → Run workflow** with the default `mbatch8` profile. Download that run's artifact, extract its `.tar.gz` into the project's `build/` directory, then launch. See the [GitHub Actions instructions](docs/GITHUB_ACTIONS_910B.md).

   Use binaries compiled from this source or produced by this repository's Actions run. Upstream binaries, Docker images, and the upstream release downloaded by default with `scripts/get_release.sh` do not contain these changes.

## Iteration status output

Each miner prints its first `iter` status line immediately, then at most once every **10 seconds**, after an iteration completes. A final summary is also printed when `PRL_MAX_ITERS` is reached. Status includes the total iteration count, latest scan duration, average `iter/s` since scanning started, and accepted/submitted share counts. Logging never inserts a delay into the mining loop; share submissions, pool responses, and errors are still reported immediately.

Set `PRL_LOG_INTERVAL_S` to a positive integer to change the interval, for example:

```bash
PRL_LOG_INTERVAL_S=30 ./scripts/launch.sh
# Docker helper: PRL_LOG_INTERVAL_S=30 ./scripts/run_container.sh
```

Invalid values fall back to 10 seconds. Explicit diagnostic modes such as `PRL_LOOP_PROF=1` and `PRL_RAW=1` still produce detailed output.

## Coexistence with other NPU workloads

Run the miner on the same dies as other processes and have it automatically get out of the way:
when another process enters the device, miner stops automatically. No need to restart manually.
Enable it by setting **`PRL_COEXIST_GUARD=1`** when you launch:

```bash
PRL_COEXIST_GUARD=1 ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 ./scripts/launch.sh
# Docker: add  -e PRL_COEXIST_GUARD=1  to your run (scripts/run_container.sh passes it through)
```

## Build

Requirements: 
- CANN 9.0.0 toolchain + Ascend-C compiler 
- `gcc` with `-march=armv8-a+crypto` (NEON BLAKE3)
- `cmake ≥ 3.16`.
- BLAKE3 C sources (the `blake3-1.x/c` dir — vendored, or from the cargo registry).
- cargo, The Pearl Rust workspace (`pearl-blake3`, `zk-pow`) for the proof FFI — adjust the paths in
  `proof-ffi/Cargo.toml` to your checkout.

```bash
make                     # defaults: RANK=128, MBATCH=8; builds build/{ascend_prl_kryptex, ascend_prl_k1, libpearl_hlc2.so, libpearl_proof.so}
```

Build flags (`make VAR=...`):

| Flag | Default | Meaning |
| ---- | ------- | ------- |
| `RANK` | `128` | Noise rank / kernel config. Current public pools accept `128` (K=4096). |
| `MDIM` | `16384` | 16384 keeps the pool's block-proof fast (non-orphaning). |
| `MBATCH` (kernel) | `8` | Strips batched per cube IterateAll (kernel-side). |

## Performance and shapes

The cube unit can not perform XORs and shifts, and the vector is bad at multiplying matrices. Thus we need to transfer
each k-partial from cube to vector through L2 (2TB/s aggregated). This limits TH/s ~ OP/s / 2 = (memory bandwidth) * (fold arithmetic intensity). We transfer 8/r bytes in total -> arithmetic intensity = r/8. So the silicone peak TH/s is rank / 8 TH/s.

Coin specs allow r up to 1024, but k >=r * 16, ZK-proof size and check time scales linearly with k. Thin increase block processing time, and therefore - probability of orphan blocks. Current public pools accept only r=128. Other ranks are kept only for custom/non-pool experiments.

> Note that **Ascend 310** and **950** do not have these limitations and can (theoretically) reach closer to hardware peak TOP/s.

## GZIP v2 + custom share difficulty (Kryptex)

Network traffic for share submits is reduced **~80–100×** by the Pearl stratum gzip protocol
([spec](https://gist.github.com/maxmalysh/eaaf4332dbc5ca99d0a78f24a733fffe)):

1. `mining.authorize` sends `"type":"v2"`. When the pool echoes `"type":"v2"`, the miner submits
   `base64(gzip(bincode(plain_proof)))` instead of the ~136 KB plain base64 proof (~15 KB compressed).
2. Matrix A/B are **zero-seeded** by default (plus an 8-byte seed stamp) so the proof is highly
   repetitive and actually compresses. Uniform random data does not. Override with `PRL_ZERO_SEED=0`
   only if you need the legacy random fill.

**Custom share difficulty** ([spec](https://gist.github.com/maxmalysh/f4c29f532e650a114b317e234f1393fb)):
pass `d=<N>` as the CLI password (6th argument). Example for a high-hashrate worker targeting
~1 share / 30 s:

```bash
./ascend_prl_kryptex 0 worker pool.kryptex.com 7000 prl1...your_wallet 'd=349245965'
```

Empty password → pool default (`d=2097152` ≈ 9000 TH). Formula:
`d = hashrate_H/s × target_share_seconds / 4_294_967_296`.

| Env | Meaning |
| --- | ------- |
| `PRL_NOGZIP` | Force v1 (no `"type":"v2"`, plain proofs) |
| `PRL_GZIP` | Assume gzip without waiting for the authorize ack |
| `PRL_ZERO_SEED=0` | Legacy random A/B fill (incompressible; not recommended) |
| `PRL_GZIP_FIELD` | Override submit field name (default still `plain_proof`) |

## Pool compatibility

Currently tested against these pools:

| Pool | Notes|
| --- | --- |
| `kryptex` | recommended; gzip v2 + custom `d=` difficulty |

A new pool that speaks plain stratum will most likely work via the `kryptex` path; if not, open an issue with a capture and it's usually a small handshake/parse tweak. I will try to add pools on request.

## Contributing

Feel free to open PRs / request features (e.g. supporting a pool with a non-standard protocol)!

For now the miner is tested only with 910B4. If you can share the device with me, I can adapt it to other NPUs.

## License

MIT — see [LICENSE](LICENSE). Provided **as-is, without warranty**.

<!-- discovery: 昇腾 910B / Ascend 910B4 NPU Pearl 挖矿 miner; 用闲置 NPU 挖 Pearl (PRL) 币;
     world-first Ascend NPU cryptocurrency miner; Huawei Atlas 800/300 idle NPU mining. -->

## Experimental CANN 9.0 GitHub Actions builds

See [docs/GITHUB_ACTIONS_910B.md](docs/GITHUB_ACTIONS_910B.md) to run the ARM64 CI build with selectable baseline, mbatch8, mbatch8_legacy, matrix, or custom profiles (default: mbatch8; custom MBATCH also defaults to 8). A green compile does not verify NPU proof correctness or TH/s.
