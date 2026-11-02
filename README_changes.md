# SIMD hash 整合版

已直接使用 Vector SIMD BLAKE3，移除 hash 開關、門檻與 kernel 內的 scalar fallback。所有有效 n_hi 均走 SIMD 路徑，計算 64 lanes，再依 n_hi 比較 target。Scalar reference 僅供 CPU 等價測試，不編入裝置 kernel。

一般建置方式：

```bash
make kernel
```

其餘建置與執行方式沿用原專案。Cube／Vector ring flag、fold、hit 編碼維持原有方式。新增 hash 狀態使用 4 KiB UB。

驗證：

```bash
python3 tests/check_simd_hash.py
```

6,592 組 lane hash 與原 scalar reference 相符；CPU shim 驗證數學與排列，不驗證裝置同步。尚未在 CANN／NPU 編譯或測速，尤其小 n_hi 仍需實測效能。
