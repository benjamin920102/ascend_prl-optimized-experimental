# 獨立 Vector / Cube / UB / 同步計時

由 kernel 的 `AscendC::GetSystemCycle()` 自行打點，每個 block 私有累計，Host 在
Cube 和 Vector 都完成後取回。直接輸出終端摘要、CSV 與選用的 Chrome Trace JSON。
執行、收集和彙整都由本專案完成。

## 照原本 GitHub Actions 編譯與啟動即可

1. 將本 ZIP 內的專案檔案更新到原 repository，包含 `.github/workflows/main.yml`。
2. 在原本的 **Build and Pre-release** workflow 按 **Run workflow**；不需要新增任何輸入欄位。
3. 下載該次 `build-aarch64.tar.gz`，用裡面的新 binary 與 `libpearl_hlc2.so` 取代舊版本。
4. 用原本相同的指令啟動，CLI、WALLET / POOL 等原有設定照舊；不用新增 profiling 參數或環境變數。

workflow 原本的 `make RANK=128` 已會預設編入插樁，Makefile、CMake 和 header 的預設值
一致開啟。一般 `make` 或 `scripts/build.sh` 也會使用相同預設。workflow 在建置前執行
Host-only profiling 測試，Release 壓縮檔另包含 `profile_summary.py` 和本說明。

程式啟動後自動啟用 mode 2 的 Vector / MTE 同步完成區段量測，記錄每個 process 前 4 個
strip。所有插樁區段都累計統計，trace 每 worker / strip 保留前 4096 筆；滿載時會明示
trace_dropped，CSV 統計仍完整累計。mode 2 會增加同步和耗時，是診斷版預設行為。

自動輸出到啟動目錄下的 `profile/`，每 process 使用 device / PID 前綴。直接執行 binary
可在終端看到 `[pearl-prof]`；原本的 launch.sh 會把同樣內容寫進 `prl-dev<N>.log`。
例如原本就是用 launch.sh 啟動時，仍是：

```bash
./scripts/launch.sh
```

要讀取既有 log 或離線彙整已產生的檔案，可使用下列工具；這些不是啟動參數：

```bash
tail -f prl-dev0.log
# 在 source tree 中：
python3 scripts/profile_summary.py profile/*_stats.csv
# 若使用 Release 內附的工具：
python3 profile_summary.py profile/*_stats.csv
```

容器也要使用包含新 library 的 image；輸出在容器內相同的相對目錄。
後文保留進階選項供除錯，但正常使用完全不需要設定。

## 時間的意義

| 設定 | 量測方式 | 用途 |
| --- | --- | --- |
| `PRL_PROF=1` | 保留原有 device 同步，量測 Scalar / API 區段 | 找 flag 等待、poll 次數、hash / UB scalar reads 與 API 發射成本 |
| `PRL_PROF=2`（預設） | 公開 Vector / MTE 操作前後加入 `V_S` / `MTE2_S` / `MTE3_S` 完成等待 | 在序列化條件下量 UB 操作和搬移；另量每次 DCCI 與 backoff |
| `PRL_PROF=0` | 不配置 / 記錄 / D2H profiling 資料 | 比較輸出結果 |
| `PROFILE=0` 建置 | 編譯移除計時、統計狀態與新增 profiling event / D2H | 正式效能基線 |

mode 1 的 `DataCopy`、Vector op、`PipeBarrier` 與 queue API 耗時，不能直接解讀為
該硬體流水線的完整執行時間；queue 同步可能在其他流水線執行。
mode 2 對公開的 Vector / MTE 操作加上 Scalar 完成等待，量到的區段包含 event、計時與
記帳成本，並非純指令 latency。Cube Matmul 仍以 library API 邊界量測。

兩種模式都會改變排程、寄存器壓力與 cache 流量；trace 會新增 GM scalar stores。
mode 2 進一步減少 overlap。正式吞吐請用 `PROFILE=0` 重新建置後量測。

## 插樁覆蓋

| 路徑 | 記錄內容 |
| --- | --- |
| Cube ring slot 背壓、Vector 等 partial | `flag_spin_wait` 總時間、`flag_poll_count` 次數、backoff 總迴圈數 |
| 每次 poll 內部（mode 2） | `flag_dcci_and_load` 與 `backoff_delay` 各自時間；與 flag wait 為父子區段 |
| Producer / consumer 發布 | 每次 `flag_store_flush` 耗時與 4-byte 邏輯寫入 |
| GM → UB | 每次 partial DataCopy，包括 look-ahead 預取 |
| UB queue | Alloc / EnQue / DeQue / Free 各次 API 時間 |
| 提早釋放 shared slot | 原有 MTE2_S SetFlag / WaitFlag 區段 |
| UB 初始化 / 累加 | Duplicate、Add 呼叫數、時間、邏輯讀寫 bytes |
| UB XOR fold | 所有 half、末段折疊及 transcript 更新中的每次 Or / And / Sub |
| UB rotate | ShiftLeft / ShiftRight / Or 分開計時 |
| 原有 barrier | 每個 PIPE_V / PIPE_ALL 都計數與計時 |
| UB → GM | 診斷 transcript export；production fast path 原本沒有此流量 |
| UB → Scalar | 每個 tile 的 16 次 GetValue 合為一個區段，64 bytes，trace 保留 tile_hi |
| Scalar | 每 tile 的 BLAKE3 / target comparison 分開計時，key / target 載入另計 |
| hit 輸出 | 每個 Vector block 的寫入與 cache flush |
| Cube Matmul | register / shape、SetTensorA、SetTensorB、每 fold IterateAll、End |
| 插樁自身 | mode 2 的 profiling_added_fence；Host 記 D2H bytes 及收集 / 輸出成本 |

Cube 可量測的程式邊界是 `GM operands → Matmul library → GM partials`。
內部 `GM → L1 → L0A/L0B → L0C → FIXPIPE → GM` 由 CANN Matmul library 實作；
附件沒有其原始碼或逐指令回呼。報表記錄 IterateAll API 區段與邏輯 operands，
**無法分別量測內部各段的時間、真實匯流排流量、bank conflict 或 cache hit rate**。
若要逐筆量測這些內部搬移，需要針對實際安裝版本的 Matmul 實作加點；此報表沒有推造數值。

## 邏輯 bytes 的計算

read / write bytes 是來源程式的 operand 存取量：

- Add 計兩份來源讀、一份目的寫，即使 acc 同時是來源和目的也各計。
- XOR 中 Or / And / Sub 各計兩份來源讀與一份目的寫，包括 scratch 中間張量。
- DataCopy 在來源計讀、目的計寫，各一次。
- Matmul 每次 IterateAll 計 `(MM_M+MM_N)*MM_K` 個 int8 input bytes 與 `MM_M*MM_N*4` output bytes。
  重複使用的 B 按邏輯 operands 計數，實際 L1/L2 重用可能減少物理傳輸。
- 每次 flag poll 計 4-byte 邏輯讀；DCCI cache-line 流量不冒充實測 bytes。
- 編譯器產生的 spill / cache writeback 等額外流量不在 operand bytes 裡，仍可能影響計時。

## 輸出與時間線

每個 process 的檔案前綴為 `dev<device>_pid<PID>`：

| 檔案 | 內容 |
| --- | --- |
| `*_stats.csv` | 每 sample / scan / strip / block / stage 的 calls、ticks、sum / min / max / mean us、來源 / 目的與讀寫 bytes |
| `*_host.csv` | Host submit 排隊時間、completion event 等待時間、profile D2H bytes、收集 / 輸出時間 |
| `*_metadata.json` | 編譯形狀、clock、模式、queue depth、trace 容量、額外 device / pinned Host bytes |
| `*_sample<N>_trace.json` | kernel 與 source-region 事件，含 band / fold / ring slot / tile_hi / bytes |

Chrome 的 `chrome://tracing` 可載入 trace JSON，查看同一卡各 block 的 source-region
時間線與重疊情況。這是程式區段時間線，並非硬體逐指令流水圖。每 sample 以所有 block
最小的系統時間戳作共同起點；不同 device / process 不自行拼為同一時鐘。

每 worker 統計區以 512 bytes 對齊，trace 每筆 64 bytes，避免跨核 atomic 聚合。
profiling-only 的 Cube completion event wait 排在 Vector kernel **之後**，確保 Cube
最後一次 flag 發布後的 profile flush 也完成，再做 D2H。

`sum_us` 是所有 worker 累計；`max_block_us` 是該類 kernel 最慢 block。
kernel_total 包含子區段；kernel_setup 包含 key load / Matmul setup；mode 2 的
added fence、poll detail 也與父區段重疊。**不能把所有 stage 耗時加總成總執行時間。**
kernel_total 不含最後寫出 / flush 統計與 trace 的成本，這會延長 kernel 與完成事件。
`flag_poll_count` / `backoff_loop_iterations` 的 calls 為次數，本身 ticks=0。
Host API 排隊與等待時間也不能當成 device copy 的獨立 latency。

GetSystemCycle 在本專案目標平台按 50 MHz 換算：`us=ticks/50`，解析度 20 ns。
短區段可能是 0 ticks；沒有扣除計時呼叫成本，也不以 AI Core 頻率換算。

## 開關、取樣與容量

| 變數 | 預設 | 說明 |
| --- | --- | --- |
| PRL_PROF | 2 | 0 關、1 原同步、2 公開 Vector / MTE 完成同步 |
| PRL_PROF_STRIPS | 4 | 每 process 最多記錄幾個 strip；0 不限 |
| PRL_PROF_EVERY | 1 | 每隔幾個 process strip 取樣；跨 scan 不歸零 |
| PRL_PROF_TRACE | 4096 | 每 worker / strip trace 上限，0..16384；0 只輸出統計 |
| PRL_PROF_OUT | ./profile | 輸出目錄，自動建立多層目錄 |

被取樣 strip 中所有實際執行的插樁操作都持續累計。原有 hit 後的 hash early-exit 保留，
不為填報表多做 hash。全程統計設 `PRL_PROF_STRIPS=0 PRL_PROF_EVERY=1`。
trace 滿後保留前面的事件並增加 trace_dropped，**摘要統計仍持續累計全部呼叫**。
mode 2 的 busy polling 可能很快填滿 trace。

device 和 pinned Host 各自增加：

```text
PIPE_DEPTH * GRID * (1+VEC2X) * (PEARL_PROF_WORKER_BYTES + PRL_PROF_TRACE * 64)
```

目前 worker stats=2048 bytes。GRID=20 / VEC2X=2 / PIPE_DEPTH=4 時：
只統計各約 0.469 MiB；trace=4096 各約 60.469 MiB；trace=16384 各約 240.469 MiB。
每個啟用插樁的 block 另有統計狀態陣列，可能造成 compiler spill，以實際資源報告為準。
原始 Vector 顯式配置的 UB buffers 為 151,552 bytes（148 KiB），沒有新增 TPipe UB buffer；
metadata 的此值不等同編譯後總 UB 使用量。

## 結果一致性檢查

Host-only tests 使用 CPU doubles 與 sanitizer，驗證格式、緩衝區邊界、trace 截斷、
mode 開關、collector 和離線解析：

```bash
make profile-tests
```

另附不連礦池、不依賴 Rust proof 的 deterministic probe，跑兩輪 scan、跨 host slots
重用與 ring wrap。相同輸入逐 byte 比對 transcript 與 hit：

```bash
make kernel profile-probe
bash tests/run_profile_equivalence.sh

# 先做小型 smoke test：
DEVICE=0 NBANDS=3 STRIPS=2 NHI=4 bash tests/run_profile_equivalence.sh
```

腳本比較 PRL_PROF=0/1/2、zero / all-ones target、ptr / copy / fast 三條 wait 路徑。
fast 比較 hit，其他路徑比較完整 transcript 與 hit。預設 nbands=83、strips=6、n_hi=64、
scans=2，mode 2 會慢。probe 包含初始化與輸入生成，不是吞吐 benchmark。
zero target 仍保留真實 hit，不硬編碼一定無命中。

可再用相同形狀的原始 library 比較修改前後：

```bash
# original_lib 放修改前 libpearl_hlc2.so；先建立 profile 目錄。
mkdir -p profile
LD_LIBRARY_PATH=./original_lib:${LD_LIBRARY_PATH:-} \
  ./build/profile_probe profile/original.bin 0 83 6 64 zero ptr 2
PRL_PROF=0 ./build/profile_probe profile/instrumented_off.bin 0 83 6 64 zero ptr 2
cmp profile/original.bin profile/instrumented_off.bin
```

本修改保留整數運算、hash、target comparison、flag 條件、partial layout 與 hit encoding。
只有實機比較通過後，才可聲稱對該硬體 / CANN 版本與測試輸入結果相同。

## API 依據與交付驗證狀態

- [CANN 9.0.0 GetSystemCycle](https://www.hiascend.com/document/detail/zh/canncommercial/900/API/ascendcopapi/atlasascendc_api_07_0282.html)：系統時鐘與 Scalar pipeline 量測邊界。
- [Ascend C API](https://www.hiascend.com/document/detail/zh/canncommercial/900/API/ascendcopapi/atlasascendc_api_07_0003.html)：SetFlag / WaitFlag 為核內跨流水同步。

已執行 Host-only sanitizer / 輸出解析測試與 Python / shell / probe 語法檢查。
製作環境沒有 CANN / Ascend 編譯器或 NPU，因此未執行 NPU 建置與實測。
ZIP 不含虛構耗時；真正的 CSV / trace 會在實機執行後產生。
