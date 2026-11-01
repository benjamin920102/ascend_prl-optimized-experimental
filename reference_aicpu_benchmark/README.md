# Ascend 910B AICPU BLAKE3 NEON benchmark

Uses actual BLAKE3 compression on AICPU, 4 independent 64-byte messages in parallel using NEON uint32x4_t intrinsics. This is NOT FMA or a fake hash. It is a short-message BLAKE3 benchmark (one block per independent hash) and does not implement the multi-block/tree BLAKE3 interface.

## 實機調校結果：6 Streams 效率最佳（使用者實測）

**目前在這台 Ascend 910B、此版本 BLAKE3 AICPU NEON 程式、相同 `groups` 設定下，使用者觀察到 `6 Streams` 的效率最高；少於 6 或多於 6，效率都會下降。** 因此預設與建議起始值均為 **6 Streams**：

```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
bash build.sh 6 1024 0     # 第一次：編譯並執行；依序為 Streams、groups、device
# 已編譯後：
./blake3_bench 6 1024 0
```

### 為什麼增加 Streams 不一定更快？

- **Streams 少於 6：並行工作可能不夠。** Host 每波最多同時送出 `streams` 個彼此獨立的 AICPU Kernel；數量較少時，可被排程的工作也較少，可能讓部分 AICPU 執行資源閒置。這是符合測試結果的合理解釋，並非已證實的核心映射。
- **6 Streams：在本機觀察到最佳平衡。** 目前工作負載能提供足夠的獨立 BLAKE3 任務，而排程、同步及共用資源的額外成本仍可接受。**6 並不等於已證實機器恰好有 6 顆可用 AICPU 核心**；一條 Stream 也不一定固定綁定一顆核心。
- **Streams 多於 6：可能出現邊際效益遞減甚至倒退。** 更多 Kernel 會競爭 AICPU 的執行時間、快取／記憶體頻寬及 Runtime 排程資源；Host 也要建立、派送、同步更多工作。若可用執行資源已飽和，這些成本可能大於新增並行帶來的收益。
- **此程式還有每波同步屏障。** `main.asc` 每波向各 Stream 各派送一個 Kernel，接著依序同步所有已提交 Stream，才開始下一波。同步與較慢 Kernel 造成的等待，也會影響 Streams 數量的最佳值。

### NEON 與 BLAKE3 原理

BLAKE3 核心使用 32-bit 整數的 **ADD／XOR／ROTATE**（ARX）運算，並非浮點 FMA。此測試的 `uint32x4_t` NEON 向量讓一條 128-bit 向量的 4 個 lane 各自處理一筆**獨立的 64-byte 訊息**，以資料層級平行化提高單一 AICPU Kernel 的處理量。Host 端的多 Streams 提供另一層任務並行。兩層加速效果都受編譯器、AICPU 排程及硬體資源限制，不保證隨 Streams 線性提升。

**「6 最佳」是目前這台機器與目前工作負載的實測結論，不保證其他 910B 子型號、其他 `groups`、CANN 版本或其他程序同時使用裝置時相同。**

### 建議如何公平重測

固定 `groups=1024`、裝置編號與其他負載，分別執行 `4 / 6 / 8 / 12 Streams`，在相近的觀測時間比較：

1. 同時執行 `npu-smi info watch`，紀錄 **`AI Cpu(%)`**；不要用 `NPU Util(%)` 代替。
2. 輸出中的 `BLAKE3 hashes` 與 `data` 是**累積量，不是每秒吞吐量**。若要量化效率，需要另外使用相同時間的 `hashes/sec` 或 `GiB/sec`；不要直接比較不同時長的累積 hash 數。
3. 確定啟動時 `BLAKE3('abc') self-test: PASS`，否則不採納性能數據。
4. 如改大 `groups`，每個 Kernel 執行時間會增加，`Ctrl+C` 停止前等待當前波完成的時間也可能增加。

## Start
```bash
source /usr/local/Ascend/ascend-toolkit/set_env.sh
bash build.sh 6 1024 0
# or afterwards:
./blake3_bench 8 4096 0
```
Parameters: `streams` default 6, range 1..32; `groups` default 1024, range 1..1000000; `device` default 0. Each group computes FOUR independent 64-byte BLAKE3 digests. More groups means longer kernel execution and slower Ctrl+C response. Start with **6 streams x 1024** (best observed on this device); compare 4,6,8,12,16 with equal-duration throughput measurement and AI Cpu(%) via `npu-smi info watch`. Excessive number of streams or very large groups can reduce responsiveness/performance. Stop with Ctrl+C; host drains the current wave.

Self-test prints `BLAKE3('abc') self-test: PASS`. Expected digest: `6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85`.

**Important**: The AICPU kernel return code is runtime status and cannot be read by the host via `<<<>>>`; inspect the self-test print. If FAIL, do not use the benchmark data. Batch counts count work submitted and synchronized, but are not an independent proof of hash validity for every input. Main code uses device printf occasionally for an observable result.

No `clock_gettime` used on AICPU; only host counts total work. `NPU Util` does not indicate AI Cpu utilization. Increasing stream count does NOT guarantee AICPU core affinity or 100% occupancy. This workload stresses integer ADD/XOR/ROTATE; NEON is not inherently guaranteed faster than scalar BLAKE3 unless measured. Hashes/sec requires timing outside this version.

## Build and troubleshooting
Requires CANN / BiSheng supporting `__global__ __aicpu__`, `aicpu_api.h`, ARM NEON and dav-2201. Compilation follows Ascend C's two-file `.aicpu` + `.asc` heterogeneous compilation. Compiler/toolkit paths vary by CANN release. If your previous `build.sh` worked but this one fails due to toolchain paths, reuse the options from your working build script and change the source/object names.

No real Ascend hardware or BiSheng compiler was available in the authoring environment; compile/runtime support and achieved utilization are not verified on your device.

## 2026-11-02 self-test correction
The previous release used incorrect expected 32-bit words for BLAKE3(`abc`).
Correct digest: `6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85`.
The test now prints each mismatching word if the device yields an unexpected digest.
`507018` was seen by the user on failed self-test synchronization; this may be the result of returning a nonzero status from the device kernel, but the exact runtime interpretation depends on CANN.
This source is not verified on real Ascend hardware.
