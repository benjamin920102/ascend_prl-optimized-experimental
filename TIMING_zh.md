# ascend_prl_test：時間量測版本

此版本基於使用者提供的 `ascend_prl-main (2).zip`。計時預設開啟，保留原本的計算、proof、礦池與非同步佇列流程。未附預編譯執行檔，必須在 Ascend 主機重新編譯；不要使用 `get_release.sh` 下載的原版二進位檔，否則不會含此次修改。

## 連結錯誤修正

此修訂明確設定 `pearl_hlc2` 的 `LINKER_LANGUAGE ASC`。在混合 `.asc` 和 `prl_timing.cpp` 時，讓 CMake 繼續使用 CANN 的 ASC 連結規則，避免一般 CXX linker 缺少 CANN 搜尋路徑而出現 `cannot find -ltiling_api/-lregister/-lplatform`。

修改後需清除 `kernel/build` 並重新 configure；原本 `make kernel` 已會執行此步驟。此環境僅驗證 CMake 規則的選擇，仍需在你的 CANN 主機確認完整編譯與執行。

## 編譯與執行

沿用原專案所需的 Ascend CANN、aarch64、Rust 與 BLAKE3 建置環境；rank 等設定仍以原 README 為準。

```bash
bash scripts/build.sh
# 或依原環境執行 make RANK=128

# 先測少量輪次；以下主機、錢包與 worker 請自行替換。
PRL_MAX_ITERS=10 bash scripts/run_timing.sh \
  ./build/ascend_prl_kryptex 0 worker pool.example.com 7000 YOUR_WALLET x

# K1 可改用 ./build/ascend_prl_k1。
# 統計時預設排除 round 0；可接受多個 CSV。
python3 scripts/timing_summary.py timing_logs/*.csv
python3 scripts/timing_summary.py --skip-rounds 0 timing_logs/某次測試.csv
```

也可以直接執行原本的 binary／`scripts/launch.sh`，計時仍會預設開啟；每個 miner process 預設產生各自的 `prl_timing_<pid>.csv`。多卡啟動時不要替所有 process 設定相同的 `PRL_TIMING_CSV`。

`run_timing.sh` 同時保存終端 log 與 CSV。CSV 逐筆 flush；若 Ctrl+C 或外部終止導致沒有最終摘要，仍可使用 Python 腳本分析已寫入的完整紀錄。正常退出會印出累計摘要；`PRL_MAX_ITERS` 達標後，程式仍會等待已啟動的 prep 與 proof 完成，這些尾端紀錄也會保存。

## 開關

| 環境變數 | 預設 | 說明 |
|---|---|---|
| `PRL_TIMING` | `1` | `0` 關閉新增的紀錄、摘要與裝置 events；主機端仍有少量時鐘讀取開銷 |
| `PRL_TIMING_PRINT` | `1` | 每筆印到 stderr；設 `0` 只保存 CSV 和摘要，可降低終端 I/O 干擾 |
| `PRL_TIMING_DEVICE` | `1` | `0` 關閉額外裝置端計時 events，保留主機端計時 |
| `PRL_TIMING_CSV` | `prl_timing_<pid>.csv` | 指定 CSV 路徑；`0` 不寫 CSV；目錄需存在，檔案需是新檔 |
| `PRL_TIMING_SUMMARY_EVERY` | `10` | 每幾輪印累計 count／total／avg／min／max；`0` 僅正常退出時印 |
| `PRL_MAX_ITERS` | 原程式設定 | 建議測試時限制輪數 |

若 CSV 路徑無法建立或已存在，會明確提示並繼續執行；不會覆蓋舊結果，也不會假裝已保存。舊的 `PRL_LOOP_PROF` 仍可使用，但會與新增紀錄重複。

## 記錄位置與解讀

時間單位全部是 **ms**。CPU 時間使用 `CLOCK_MONOTONIC`，NPU 階段使用各自 stream 的 timeline event。裝置 event 的結果在原本的完成等待之後收集，不插入新的 stream wait 或裝置全域同步。

| 指標 | 定義 |
|---|---|
| `round.prep_join_wait` | 主執行緒等待本輪 prep thread 完成的時間，包括資料尚未準備好、B 尚未上傳等情況 |
| `round.coexist_wait` | 共存模式的暫停等待，避免誤判為計算瓶頸 |
| `round.select_B_host` | 選擇 B、提交重設操作的 host 耗時；fallback 路徑可能包含 B 上傳 |
| `round.target_cpu` | 本輪 target 建立 |
| `round.scan` | `scan_full` 整體時間，包含提交、等待、CPU 檢查與命中排空 |
| `round.total` | 本輪開始等待 prep → scan 完成並處理 proof 入列；不包含接下來的摘要和一般狀態列輸出 |
| `round.start_interval` | 相鄰兩輪開始時刻之差，包含中間輸出、排程及重連等待；記錄在後一輪 ID 下 |
| `prep.bundle_hold_wait` | prep 等待上一筆 proof 釋放該 bundle |
| `prep.requested_delay` | 非 reuse-B 路徑中使用者設定的 prep 延遲及其少量處理開銷 |
| `prep.key_snapshot` | 取得工作狀態、建立 key，包含 job mutex 等待 |
| `prep.fill` | 每次原始 A／B 資料填入；A/B 共用同一指標 |
| `prep.root_A`、`prep.root_B` | 各自 root 計算；可能平行執行 |
| `prep.noise_seed`、`prep.permutation` | 每次 noise seed／permutation 生成；A/B 共用指標 |
| `prep.apply_A` | A 加噪 |
| `prep.apply_B_pack`／`prep.apply_B_transpose` | B 加噪與打包／轉置，依實際路徑記錄 |
| `prep.compute` | prep API 整體時間；reuse-B 時只含 A-side prep |
| `prep.B_upload_host` | prep 端呼叫 B 上傳的 host 耗時，包含原本已有的完成等待 |
| `prep.total` | prep worker 開始到準備完成，包含 bundle 等待、指定延遲、計算及 B 上傳 |
| `B.repack_cpu` | 舊 B API 路徑的 CPU repack；packed-B 路徑沒有此步驟 |
| `B.upload_host_including_wait` | B copy 提交到原本的 B stream 同步返回 |
| `device.B_H2D` | B stream 中 B 上傳區間 |
| `scan.key_target_submit` | key／target 上傳與 readiness event 的 host 提交耗時，非裝置傳輸時間 |
| `strip.submit_host` | 單段 strip 的 copy、kernel、event 等 API 提交總開銷 |
| `strip.submit_to_observed_done` | 單段開始提交 → CPU 在原本的 wait 路徑觀察完成；包括排隊及延後觀察時間 |
| `strip.inflight` | `value` 是該次提交後尚未被 host 回收的 strip 數；不是 NPU 正在同時計算的 kernel 數 |
| `device.A_H2D` | Cube stream 中 A strip 上傳區間 |
| `device.Cube_including_stalls` | Cube kernel 區間，包括 kernel 內 producer／consumer 等待及其他停頓 |
| `device.Vector_including_stalls` | Vector kernel 區間，包括等待 Cube 產生 partials 的時間 |
| `device.result_D2H` | Vector stream 中結果回傳；使用 transcript API 時也包含 transcript 回傳 |
| `scan.wait_done_host` | 主掃描迴圈等待一段完成，包括收集該段計時的開銷；命中排空由下列指標另計 |
| `scan.reduce_hits_cpu` | CPU 彙整各 Vector block 的命中結果 |
| `scan.hit_drain` | 發現命中後等待剩餘已提交 strip 的時間；`value` 是額外排空的 strip 數 |
| `scan.completed_strips` | `ms=0`；`value` 為該輪實際完成 strip 數，包含命中後排空 |
| `proof.enqueue_wait` | 等 proof queue 空位，包含取得 queue mutex 等待 |
| `proof.queue_wait` | 成功保留佇列位置 → worker 出列後開始處理 |
| `proof.build` | 原有 Rust FFI 建立 proof 的整體時間；`value` 是成功產生的 bytes，未細拆 FFI 內 Merkle／序列化 |
| `proof.gzip`、`proof.base64` | 分開量 gzip／base64；未協商 gzip 時不產生 gzip 紀錄 |
| `proof.encode` | 包含 buffer 配置、gzip／base64 的整體編碼時間；`value` 是成功編碼的 bytes |
| `proof.socket_send` | 原本 socket 發送函式耗時；不包含礦池接受 share 的往返時間；`value=0` 表示該呼叫成功 |
| `proof.worker_total` | 該 proof 的 worker 處理耗時，失敗／過期丟棄也會計入 |
| `proof.enqueue_to_release` | 入列 → 完成處理並準備釋放 bundle，包含排隊 |
| `proof.drain_wait` | B refresh、重連或退出前等待 proof queue 與 worker 清空 |
| `reuse_B.refresh_and_upload` | reuse-B 模式下更新 B 及上傳 |
| `reuse_B.redo_A` | B 更新後，重新生成與新 B commitment 相符的 A |

CSV 欄位：`end_monotonic_ms,pid,tid,round,strip,slot,metric,ms,value`。

- `round` 從 0 開始，於 prep 啟動時分配，prep／scan／proof 可以跨執行緒對應。結束時可能多一組已準備但未掃描的 round。
- `strip=-1` 代表整輪／prep 階段；其他值為 scan 內 strip 編號。
- `slot` 在 prep／proof／round 記錄表示 bundle／B slot，在 strip／device 記錄表示 strip buffer slot。不能把這兩者當成同一種 slot。
- `tid` 為 Linux 執行緒 ID，root 的 OpenMP 子執行緒也會帶上正確 round。
- `end_monotonic_ms` 是 host 寫入紀錄時的單調時鐘時間。對 device 指標而言，這是收集時間，**不是裝置端事件的絕對時間戳**；不可直接用它重建 Cube／Vector 的重疊 timeline。
- `value` 不是通用計數：依上表可能是 bytes、在途數、完成 strip 數或錯誤碼。摘要的 `sum_value` 需依指標解讀。

## 優先觀察

1. `round.prep_join_wait` 與 `prep.bundle_hold_wait`：分辨下一輪資料準備不足，或 proof 持有 buffer 太久。
2. `device.Cube_including_stalls` 與 `device.Vector_including_stalls`：找出整段執行差異；兩者會重疊，不能相加當作總計算時間。
3. `strip.submit_host`、`scan.wait_done_host`、在途數：檢查 host 提交成本與流水線是否填滿。
4. `scan.hit_drain`：評估增加 PIPE_DEPTH 帶來的命中後額外工作。
5. `proof.queue_wait`、`proof.worker_total`、`prep.bundle_hold_wait`：檢查 proof 後端是否產生背壓。

建議先用 `PRL_TIMING_PRINT=0` 收集 CSV，再與 `PRL_TIMING=0` 的吞吐量比較，估計 instrumentation 本身的影響。詳細逐筆打印與 timeline events 都會有開銷；暖機、命中與未命中輪次應分開比較。Python 腳本僅做分指標統計，尚未自動按命中分類。

## 裝置量測的限制與回退

- timeline event 建立失敗時會提示錯誤並停用裝置計時，主機計時仍有效。
- 不新增完成等待。若收集當下 Cube 尾端 event 尚未完成，就記錄 `<原指標>.unavailable` 並略過該樣本；該項的 `ms=0` 是狀態紀錄，不能當成 kernel 為零耗時。
- event 記錄或 elapsed API 失敗時，產生 `device.event_record_error`／`device.elapsed_error`，`value` 保留錯誤碼；不捏造有效計時。
- 本版沒有在 kernel 內逐次讀 cycle counter，因此不能直接量化環形緩衝區 producer／consumer 自旋比例，也沒有新增裝置占用率採樣。需進一步用 msprof timeline／硬體指標分析。
- 可參考 Huawei 官方事件計時文件：
  https://www.hiascend.com/doc_center/source/en/CANNCommunityEdition/900/programug/acldevg/runtime_doc_dev_0023.html
  https://www.hiascend.com/doc_center/source/en/CANNCommunityEdition/900/API/runtimeapi/aclcppdevg_03_0086.html

## 驗證狀態

```bash
bash tests/run_host_tests.sh
# 需要 cmake + C++ 編譯器，使用 mock ASC 規則驗證 linker 選擇
python3 tests/cmake_link_mock.py
```

已另以 mock ASC 連結規則重現「未設定時 CXX linker 找不到函式庫」，並驗證明確設定 ASC 後可連結含 logger 的測試 shared library。此測試使用 dummy kernel 與 dummy library，不是 CANN 編譯。

已在此交付環境執行：scan mock 測試（無命中／首段／中段／末段命中、排空與佇列容量）、proof gzip/base64 與原版逐 byte 比較、停用計時模式、四執行緒 CSV 完整性與 context、摘要工具、logger 的 C／C++ 編譯、miner C 語法檢查。

交付環境沒有 Ascend 硬體、CANN 編譯器與原建置環境中的 BLAKE3 原始碼，**未完成整個專案的 CANN 編譯與 NPU 實跑**。請在目標機執行原建置流程與少量輪次測試，再評估效能數字；mock 測試不驗證裝置 API、kernel 正確性或礦池實際接受 share。
