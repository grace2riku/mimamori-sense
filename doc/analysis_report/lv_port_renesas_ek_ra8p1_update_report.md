# リファレンスプロジェクト lv_port_renesas_ek_ra8p1 更新レポート

本レポートはIssue #138の成果物である。ローカルのリファレンスプロジェクト `reference_projects/lv_port_renesas_ek_ra8p1` とGitHubリポジトリ最新版の比較結果、ローカルの更新内容、およびmimamori-sense本体ソフトウェア（`e2studio_CPU0`）への反映内容をまとめる。

- **実施日**: 2026-06-02
- **Issue**: #138

---

## 1. 比較結果（Phase A）

### 1.1 バージョン情報

| 項目 | 更新前（ローカル） | 更新後（GitHub最新） |
|---|---|---|
| コミット | `522075e5303228b6576cc399a82fe5adad35693e` | `6ab5aa5a925025bd086127089faeb6f9765d801b` |
| コミット日 | 2025-12-01 | 2026-02-17 |
| コミットメッセージ | "upgrade to FSP v6.2.0" | "Merge pull request #2 from jeremy-baker/ospi_flash_support" |
| FSPバージョン | 6.2.0 | **6.3.0** |
| ブランチ | master | master |

### 1.2 差分コミット一覧（6コミット、86ファイル変更）

| コミット | 日付 | 内容 |
|---|---|---|
| `4a26c79` | 2025-12-15 | Add OSPI Flash support |
| `8d5e3c2` | 2026-01-07 | Update to FSP 6.3.0 |
| `cfc8a35` | 2026-01-07 | Remove LVGL files from being tracked, cleaning up |
| `47b0e35` | 2026-01-08 | Reduce CPU usage in benchmark demo |
| `0292ebd` | 2026-01-08 | Remove unused variable |
| `6ab5aa5` | 2026-02-17 | Merge pull request #2 from jeremy-baker/ospi_flash_support |

### 1.3 主な変更内容

#### (1) OSPI Flash対応（`4a26c79`, `6ab5aa5`）

LVGLベンチマークのアセットデータを外部OSPI Flashに配置する対応。

- 追加: `src/ospi_flash.c` / `src/ospi_flash.h`（OSPI初期化・メモリマップドアクセス）
- 追加: `RA8x1_Reset_OSPI.JLinkScript`、`generate_ospi_srecord.bat`、`srecord/`（書き込みツール）
- FSP設定: `g_ospi_flash OSPI (r_ospi_b)` インスタンス追加、OCTACLKをPLL2P/3に変更
- `new_thread0_entry.c` で `init_opsi_flash()` を呼び出し

#### (2) FSP 6.3.0への更新（`8d5e3c2`）

- `configuration.xml`、`.cproject`、`ra_cfg.txt` 等をFSP 6.3.0向けに更新
- クロック設定の変更: PLL2 Mul x300→x266、USBCLK/USB60CLK/ETHPHYCLK無効化

#### (3) FSP/LVGL由来ファイルのGit管理除外（`cfc8a35`）

上流の管理方針変更。以前は改変したFSP/LVGLファイルをGit管理していたが、改変をやめてFSP標準ファイルを使う方式に変更。

- 削除（Git管理から除外）:
  - `ra/fsp/src/r_drw/r_drw_memory.c`（Dave2Dメモリ管理。改変版を管理していた）
  - `ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_image.c`
  - `ra/lvgl/lvgl/src/libs/tiny_ttf/`（lv_tiny_ttf.c, stb_rect_pack.h, stb_truetype_htcw.h）
  - `ui/` ディレクトリ全体（LVGL Editor生成物）
- 代替として追加: `src/dave2d_cache_management.c`
  - FSP標準の弱参照関数 `d1_cacheflush()` / `d1_cacheblockflush()` をオーバーライドし、
    dcache有効時（`BSP_CFG_DCACHE_ENABLED=1`）にデータキャッシュをフラッシュする
  - 改変版 `r_drw_memory.c` をGit管理する必要がなくなった

#### (4) ベンチマークデモのCPU使用率削減（`47b0e35`）

- 追加: `src/User_FreeRTOSConfig.h`（FreeRTOSタスクスイッチのトレースフック）
  - `traceTASK_SWITCHED_IN/OUT` → `lv_freertos_task_switch_in/out()` を呼び、
    LVGLパフォーマンスモニタのCPU使用率計測精度を向上
- FSP設定: FreeRTOS「Use Trace Facility」有効化、「Custom FreeRTOSConfig.h」に上記を指定
- `src/lv_conf_user.h` の簡素化（LV_USE_LOG無効化、Montserratフォント追加等）

#### (5) 未使用変数マクロの削除（`0292ebd`）

- `src/port/lv_port_disp.c` の `glcdc_flush_finish_event()` 内の `FSP_PARAMETER_NOT_USED(event);` を削除
  - `event` は実際に関数内で使用されているため、このマクロは矛盾していた

---

## 2. リファレンスプロジェクトの更新内容（Phase B）

### 2.1 更新方法

ローカルの `reference_projects/lv_port_renesas_ek_ra8p1` はGitクローンではなくファイルコピーのため、以下の手順で更新した。

1. GitHubリポジトリ最新版（`6ab5aa5`）を一時ディレクトリにクローン
2. Git管理下の全56ファイルをローカルへコピー（上書き）
3. `522075e` → `6ab5aa5` で削除された54ファイルをローカルからも削除
4. 全ファイルの一致を `cmp` で検証（全ファイル一致を確認）

### 2.2 更新後の注意点（重要）

上流の管理方針変更（1.3 (3)）により、以下のファイルがローカルから削除された。
**リファレンスプロジェクトをビルドする前に、e2 studioで「Generate Project Content」を実行してFSP/LVGL標準ファイルを再生成する必要がある。**

| 削除されたファイル | 再生成方法 |
|---|---|
| `ra/fsp/src/r_drw/r_drw_memory.c` | FSPコード生成（r_drwコンポーネント） |
| `ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d_image.c` | FSPコード生成（LVGLパック） |
| `ra/lvgl/lvgl/src/libs/tiny_ttf/*`（3ファイル） | FSPコード生成（LVGLパック） |
| `ui/` ディレクトリ | 再生成不可（上流から削除。LVGL Editorデモは廃止） |

### 2.3 ドキュメント更新

- `reference_projects/README.md` の対象ソースコード情報を更新（コミットハッシュ・日付・FSPバージョン）

---

## 3. mimamori-sense本体（e2studio_CPU0）への反映内容（Phase C）

### 3.1 反映した変更

#### (1) `src/port/glcdc_port.c`: 不要な `FSP_PARAMETER_NOT_USED(event)` の削除

上流コミット `0292ebd` の反映。

- CPU0の `glcdc_backlight_on_event()` はリファレンスの `lv_port_disp.c` の
  `glcdc_flush_finish_event()` から移植された関数であり、同じ問題（`event` を実際に
  使用しているのに `FSP_PARAMETER_NOT_USED(event)` が記述されている矛盾）を持っていた
- 変更箇所: `e2studio_CPU0/src/port/glcdc_port.c` の224行目を削除

#### (2) `src/port/dave2d_cache_management.c`: Dave2Dキャッシュ管理オーバーライドの移植（新規追加）

上流コミット `4a26c79`（`6ab5aa5` マージ）で追加された `src/dave2d_cache_management.c` の移植。

- FSP標準の弱参照関数 `d1_cacheflush()` / `d1_cacheblockflush()`（`ra/fsp/src/r_drw/r_drw_memory.c`）
  をオーバーライドし、Cortex-M85のdcache有効時にキャッシュをクリーン+無効化する
- **現状のCPU0はdcache無効（`BSP_CFG_DCACHE_ENABLED=0`）のため動作は変わらない**（no-op）
- 将来dcacheを有効化した場合（リファレンスプロジェクトが実施済みの性能改善策）、
  この実装がないとDave2D描画が乱れる潜在バグとなるため、予防的に移植した
- 配置先: CPU0の規約に合わせて `src/port/` に配置（リファレンスは `src/` 直下）

#### (3) LVGLパフォーマンスモニタの精度向上（`User_FreeRTOSConfig.h` + FreeRTOSトレースフック）

上流コミット `47b0e35` の反映。当初は任意対応としていたが、本Issue内で実施した。

- 追加: `e2studio_CPU0/src/User_FreeRTOSConfig.h`（リファレンスと同内容）
  - `traceTASK_SWITCHED_IN/OUT` マクロを定義し、LVGLの `lv_freertos_task_switch_in/out()` を呼び出す
- FSP設定変更（e2 studioで手動実施）:
  - FreeRTOS「General: Custom FreeRTOSConfig.h」→ `User_FreeRTOSConfig.h`
  - Generate Project Content により `ra_cfg/aws/FreeRTOSConfig.h` に
    `#include "User_FreeRTOSConfig.h"` が追加された
- 変更: `e2studio_CPU0/src/lv_conf_user.h` の `LV_SYSMON_GET_IDLE` を
  `lv_timer_get_idle` → `lv_os_get_idle_percent` に変更
  - トレースフックが蓄積するFreeRTOSベースのアイドル時間データを
    パフォーマンスモニタが実際に参照するようにするための変更
  - これがないとフックのデータは蓄積されるだけで使われない
    （リファレンスは `LV_SYSMON_GET_IDLE` を定義せずLVGLデフォルトの
    `lv_os_get_idle_percent` を使用するため、この問題が発生しない）
  - 効果: CPU使用率表示がLVGL処理だけでなく他のFreeRTOSタスク
    （カメラ・AI推論・NT-Shell）の負荷も反映した正確な値になる

### 3.2 反映しなかった変更とその理由

| 上流の変更 | 反映 | 理由 |
|---|---|---|
| FSP 6.3.0への更新（`8d5e3c2`） | 不要 | CPU0は既にFSP 6.3.0で構築済み |
| OSPI Flash対応（`4a26c79`） | 見送り | mimamori-senseはOSPI Flashを使用していない。LVGLベンチマークのアセット配置用であり、本体機能に関係なし |
| `lv_conf_user.h` の簡素化（`47b0e35`） | 見送り | CPU0の `lv_conf_user.h` はmimamori-sense用に意図的にカスタマイズされた設定（詳細なコメント付き）であり、リファレンスのベンチマーク用簡素化を取り込むと逆に劣化する |
| LVGLファイルのGit管理除外（`cfc8a35`） | 対象外 | リファレンスプロジェクト側の管理方針変更であり、本体には影響しない |

---

## 4. 変更ファイル一覧

### リファレンスプロジェクト（`reference_projects/`）

| 分類 | ファイル数 | 内容 |
|---|---|---|
| 変更 | 18 | configuration.xml, .cproject, ra_cfg.txt, src/*, .settings/* 等 |
| 追加 | 14 | src/ospi_flash.c/h, src/dave2d_cache_management.c, src/User_FreeRTOSConfig.h, srecord/ 等 |
| 削除 | 54 | ui/ 全体, ra/fsp/src/r_drw/r_drw_memory.c, LVGL tiny_ttf 等 |
| ドキュメント | 1 | reference_projects/README.md |

### mimamori-sense本体（`e2studio_CPU0/`）

| 分類 | ファイル | 内容 |
|---|---|---|
| 変更 | `src/port/glcdc_port.c` | 不要な `FSP_PARAMETER_NOT_USED(event)` を削除（1行削除） |
| 追加 | `src/port/dave2d_cache_management.c` | Dave2D dcacheフラッシュのオーバーライド（将来のdcache有効化に備える） |
| 追加 | `src/User_FreeRTOSConfig.h` | FreeRTOSタスクスイッチのトレースフック定義 |
| 変更 | `src/lv_conf_user.h` | `LV_SYSMON_GET_IDLE` を `lv_os_get_idle_percent` に変更 |
| 変更 | `configuration.xml` | FreeRTOS「Custom FreeRTOSConfig.h」設定（e2 studioで手動変更） |
| 変更 | `ra_cfg/aws/FreeRTOSConfig.h` | FSPコード生成による自動更新（`User_FreeRTOSConfig.h` のinclude追加） |

---

## 5. ビルド・動作確認

### 5.1 確認項目（ユーザー実施）

- [x] e2 studioでCPU0プロジェクトのビルドが成功すること（2026-06-02 確認済み）
- [x] EK-RA8P1実機でカメラ画像表示・LCD表示・LVGL UIが従来どおり動作すること（リグレッション確認、2026-06-02 確認済み）
- [ ] `LV_SYSMON_GET_IDLE` 変更後の再ビルド・パフォーマンスモニタ（画面右下のCPU%/FPS表示）が
      正常に表示されること

### 5.2 リファレンスプロジェクト側（任意）

- [ ] e2 studioでリファレンスプロジェクトを開き「Generate Project Content」→ ビルド成功を確認
  - FSP 6.3.0環境（setup_fsp_v6_3_0_e2s_v2025-12）でそのまま開ける
