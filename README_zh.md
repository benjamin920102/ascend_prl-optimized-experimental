# `ascend_prl` — 全球首个昇腾 NPU 加密货币矿工

> 每次 kernel 提交固定處理 1 個 logical strip，已移除批次大小設定。更新後請一起重新編譯 kernel library 與 miner。

> **Host context 更新：** `aclrtSetDevice` 僅在 `pearl_init` 執行。所有 stream/event 共用顯式建立的 ACL context；prep 執行緒首次使用時綁定，之後 strip/key 提交只檢查 TLS 快取。預設本地／CI 參數：`BUFS=4 VEC2X=2 BACKOFF=500 HIT_EPOCH=1`。詳見 [`docs/HOST_CONTEXT_910B.md`](docs/HOST_CONTEXT_910B.md)。


> **910B Profiler v3：** 預設 `HIT_EPOCH=1`，改為每個 Vector Core 獨立的 epoch 命中記錄，免去每次 strip 之間的 device memset。`HIT_EPOCH=0` 可作舊版 A/B 對照；GitHub Actions 有 `legacy` 和 `epoch`。詳見 [`docs/HIT_EPOCH_PROFILER_910B.md`](docs/HIT_EPOCH_PROFILER_910B.md)。此修改**尚未在實際 NPU 上驗證**。

[English](README.md) | **中文**

> **此源码版本：** 所有挖矿连接和份额提交均使用你指定的钱包；本地编译和 GitHub Actions 均固定每次提交 1 个 strip，A 输入保持原始布局直接上传。原版的性能数据不代表此版本的实测结果。

一个从零编写、面向 [Pearl](https://arxiv.org/abs/2504.09971) **有用工作量证明（Proof-of-**Useful**-Work）** 币的矿工，
运行在华为**昇腾 910B** NPU 上。已在 910B4 上实测，端到端可达 **~30 TH/s/卡**。

> **免责声明：** 仅供学习研究之用。请仅在你拥有授权的硬件上运行。

## 快速开始

1. **创建 Pearl 钱包。** 使用[官方钱包](https://github.com/pearl-research-labs/pearl/releases/tag/pearl-wallet-v2.0.0)
   并按其说明操作。你会得到一个形如下面的地址：
   `prl1...YOUR_WALLET_ADDRESS`。
2. **选择矿池。** 任何标准 `stratum` 协议的 Pearl 矿池都应可用。本项目在
   [Kryptex](https://pool.kryptex.com/prl) 上测试。注意：不同矿池接受的 M、N、K 形状不同（见[性能与形状](#性能与形状)）。
3. **编译此版本并运行。** 在已配置 CANN 的 ARM64 环境解压此源码，进入项目目录，先按[编译](#编译)安装依赖，然后执行：
   ```bash
   make                     # 默认 RANK=128
   export WALLET=prl1...你的钱包地址
   export POOL=矿池地址 PORT=7000
   ASCEND_RT_VISIBLE_DEVICES=0,1,2,3 ./scripts/launch.sh
   ```
   也可以将此源码上传至你的 GitHub 仓库，在 **Actions → Run workflow** 使用默认 `default` profile 编译。下载该次运行的 artifact，将其中的 `.tar.gz` 解压到项目的 `build/` 目录后再启动。详见 [GitHub Actions 操作说明](docs/GITHUB_ACTIONS_910B.md)。

   请使用此源码编译的程序或此仓库 Actions 生成的产物；上游发布的二进制、Docker 镜像以及 `scripts/get_release.sh` 默认下载的上游版本不包含本次修改。

## 迭代状态输出

每个矿工在首次迭代完成时立即输出一行 `iter` 状态，之后默认最多每 **10 秒**输出一次，在一轮迭代结束时检查。达到 `PRL_MAX_ITERS` 时也会输出最后的汇总。状态包含累计迭代次数、最近一次扫描耗时、从扫描开始计算的平均 `iter/s`，以及已接受／已提交份额数量。此修改只限制状态输出频率，不会在挖矿循环中加入等待；份额提交、矿池回应和错误仍立即显示。

可用正整数秒数调整间隔，例如改为每 30 秒输出一次：

```bash
PRL_LOG_INTERVAL_S=30 ./scripts/launch.sh
# Docker 脚本：PRL_LOG_INTERVAL_S=30 ./scripts/run_container.sh
```

无效数值会回到默认的 10 秒。主动开启 `PRL_LOOP_PROF=1` 或 `PRL_RAW=1` 时，仍会输出详细诊断信息。

## 与其他 NPU 任务共存

可以让矿工与其他进程跑在同一批芯片上，并自动「让路」：当有其他进程进入该设备时，
矿工会自动停止，无需手动重启。在启动时设置 **`PRL_COEXIST_GUARD=1`** 即可启用：

```bash
PRL_COEXIST_GUARD=1 ASCEND_RT_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 ./scripts/launch.sh
# Docker：在 run 命令中加上  -e PRL_COEXIST_GUARD=1 （scripts/run_container.sh 会自动透传）
```

## 编译

依赖：
- CANN 9.0.0 工具链 + Ascend-C 编译器
- 支持 `-march=armv8-a+crypto` 的 `gcc`（用于 NEON 版 BLAKE3）
- `cmake ≥ 3.16`
- BLAKE3 的 C 源码（`blake3-1.x/c` 目录 —— 已随仓库附带，或取自 cargo registry）
- cargo，以及用于 proof FFI 的 Pearl Rust 工作区（`pearl-blake3`、`zk-pow`）——
  请在 `proof-ffi/Cargo.toml` 中把路径改成你本地的检出目录。

```bash
make                     # 默认 RANK=128；生成 build/{ascend_prl_kryptex, ascend_prl_k1, libpearl_hlc2.so, libpearl_proof.so}
```

编译选项（`make 变量=...`）：

| 选项 | 默认 | 含义 |
| ---- | ------- | ------- |
| `RANK` | `128` | 噪声 rank / 算子配置。当前公开矿池只接受 `128`（K=4096）。 |
| `MDIM` | `16384` | 16384 可让矿池的区块证明（block-proof）足够快，避免产生孤块（orphan）。 |

## 性能与形状

Cube 单元无法做异或（XOR）与移位，而 Vector 单元不擅长矩阵乘法。因此需要把每个 k-partial
经 L2（聚合带宽 2TB/s）从 Cube 搬运到 Vector。这就把吞吐限制为
TH/s ~ OP/s / 2 =（内存带宽）×（fold 的算术强度）。我们总共搬运 8/r 字节 → 算术强度 = r/8。
所以硅片理论峰值约为 rank / 8 TH/s。

币种规范允许 r 最高到 1024，但要求 k ≥ r×16，而 ZK 证明的大小与校验时间随 k 线性增长。
k 增大会拉长区块处理时间，从而提高产生孤块的概率。当前公开矿池只接受 r=128。其他 rank 仅保留用于自定义/非公开矿池实验。

> 注意：**昇腾 310** 和 **950** 没有上述限制，理论上可以更接近硬件峰值 TOP/s。

## GZIP v2 + 自定义份额难度（Kryptex）

通过 Pearl stratum gzip 协议，提交份额的网络流量可降低约 **80–100 倍**
（[协议说明](https://gist.github.com/maxmalysh/eaaf4332dbc5ca99d0a78f24a733fffe)）：

1. `mining.authorize` 发送 `"type":"v2"`。矿池在应答中回显 `"type":"v2"` 后，矿工提交
   `base64(gzip(bincode(plain_proof)))`，而不是约 136 KB 的明文 base64 证明（压缩后约 15 KB）。
2. 矩阵 A/B **默认以零填充**（外加 8 字节 seed 戳），使证明高度可重复、真正可被 gzip 压缩。
   均匀随机数据几乎压不动。仅在需要旧版随机填充时设置 `PRL_ZERO_SEED=0`。

**自定义份额难度**（[说明](https://gist.github.com/maxmalysh/f4c29f532e650a114b317e234f1393fb)）：
把 CLI 第 6 个参数（password）设为 `d=<N>`。例如高算力矿机目标约每 30 秒一个份额：

```bash
./ascend_prl_kryptex 0 worker pool.kryptex.com 7000 prl1...你的钱包 'd=349245965'
```

password 为空 → 使用矿池默认（`d=2097152` ≈ 9000 TH）。公式：
`d = 算力_H/s × 目标份额间隔秒数 / 4_294_967_296`。

| 环境变量 | 含义 |
| --- | --- |
| `PRL_NOGZIP` | 强制 v1（不发 `"type":"v2"`，明文证明） |
| `PRL_GZIP` | 不等 authorize 应答就假定启用 gzip |
| `PRL_ZERO_SEED=0` | 旧版随机 A/B 填充（不可压缩，不推荐） |
| `PRL_GZIP_FIELD` | 覆盖 submit 字段名（默认仍为 `plain_proof`） |

## 矿池兼容性

目前已测试过的矿池：

| 矿池 | 备注 |
| --- | --- |
| `kryptex` | 推荐；支持 gzip v2 + 自定义 `d=` 难度 |

只要是讲标准 stratum 协议的新矿池，大概率走 `kryptex` 路径就能用；如果不行，请附上一段抓包
开一个 issue，通常只需要对握手/解析做一点小调整。我会按需添加矿池支持。

## 贡献

欢迎提交 PR / 提需求（例如支持某个非标准协议的矿池）！

目前矿工仅在 910B4 上测试过。如果你愿意把设备共享给我，我可以适配到其他 NPU。

## 许可证

MIT —— 见 [LICENSE](LICENSE)。按**「现状」提供，不作任何担保**。

<!-- discovery: 昇腾 910B / Ascend 910B4 NPU Pearl 挖矿 miner; 用闲置 NPU 挖 Pearl (PRL) 币;
     world-first Ascend NPU cryptocurrency miner; Huawei Atlas 800/300 idle NPU mining. -->

## 实验版 GitHub Actions 编译

GitHub Actions 提供 default、baseline、legacy、epoch、matrix 與 custom profiles；全部固定每次提交 1 個 strip。詳見 [操作說明](docs/GITHUB_ACTIONS_910B.md)。
