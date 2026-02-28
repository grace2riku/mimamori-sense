# e2 studio操作手順書: F-002-5: カメラキャプチャ用FreeRTOSスレッドの設定変更

## 対象Issue

- Issue #14: F-002-5: カメラキャプチャ用FreeRTOSスレッドの実装

## 対象プロジェクト

- `e2studio_CPU0/configuration.xml`

## リファレンスプロジェクト

- `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml`

## 前提条件

- Issue #10 (VIN・MIPI CSI・MIPI PHYモジュールの追加) が完了していること
  - `camera_thread` は既にIssue #10の手順で作成済み
  - VIN > MIPI CSI > MIPI PHYのスタック構成も追加済み
- Issue #13 (OV5640カメラセンサードライバの実装) が完了していること

## 現在のプロジェクト状態

### 既存スレッド一覧

| スレッド | Symbol | Priority | Stack (bytes) | 役割 |
|----------|--------|----------|---------------|------|
| Blinky Thread | `blinky_thread` | 1 | 512 | LED制御、CPU1起動 |
| NT-Shell Thread | `ntshell_thread` | 1 | 4096 | シリアルコンソール |
| LVGL Thread | `lvgl_thread` | 2 | 8192 | LVGL GUI |
| Camera Thread | `camera_thread` | **3** | **10240** | カメラキャプチャ |

### Camera Threadのスタック構成（設定済み）

```
Camera Thread (camera_thread)
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

### 変更が必要な項目

Issue #14の要件と現在の設定を比較すると、以下の2つのパラメータが異なります:

| パラメータ | 現在の値 | Issue #14の要件値 | リファレンス値 |
|-----------|----------|------------------|---------------|
| Stack size (bytes) | 10240 | **4096** | 10240 |
| Priority | 3 | **4** | 3 |

> **注意**: Issue #14で指定されているStack size (4096) とPriority (4) は、リファレンスプロジェクトの値（Stack size: 10240, Priority: 3）とは異なります。Issue #14の要件に従って変更します。

### FreeRTOS configMAX_PRIORITIES の確認

現在の`configMAX_PRIORITIES`は`5`です。FreeRTOSでは0からconfigMAX_PRIORITIES-1（つまり0-4）が有効な優先度範囲のため、Priority 4は設定可能です。

---

## 手順1: Camera Threadのプロパティ変更

`camera_thread`は既にconfiguration.xmlに追加済みのため、新規作成ではなくプロパティの変更を行います。

1. e2 studioで `e2studio_CPU0/configuration.xml` を開く
2. **Stacks** タブを選択
3. 左側のスレッド一覧から **Camera Thread** をクリックして選択する
4. 画面下部の **Properties** パネルで以下の2項目を変更する

### 設定パラメータ

| プロパティ名 | 現在の値 | 変更後の値 | 備考 |
|-------------|----------|-----------|------|
| Stack size (bytes) | 10240 | **4096** | Issue #14の要件。リファレンス(10240)より小さいが、カメラキャプチャ処理に必要十分なサイズ |
| Priority | 3 | **4** | LVGLスレッド(Priority 2)より高い優先度。カメラキャプチャのリアルタイム性を確保 |

以下のパラメータは変更不要です（現在の値のまま）:

| プロパティ名 | 現在の値 | 備考 |
|-------------|----------|------|
| Symbol | `camera_thread` | 変更不要 |
| Name | `Camera Thread` | 変更不要 |
| Thread Context | `NULL` | 変更不要 |
| Memory Allocation | `Static` | 変更不要 |
| Allocate Secure Context | `Enable` | 変更不要 |

---

## 手順2: 変更後のスレッド優先度整理の確認

変更後、全スレッドの優先度が以下のようになっていることを確認してください:

| スレッド | Priority | Stack (bytes) | 備考 |
|----------|----------|---------------|------|
| Blinky Thread | 1 | 512 | 変更なし |
| NT-Shell Thread | 1 | 4096 | 変更なし |
| LVGL Thread | 2 | 8192 | 変更なし |
| **Camera Thread** | **4** | **4096** | **変更あり** |

> **備考**: FreeRTOSでは数値が大きいほど高優先度です。Camera Thread (Priority 4) はLVGL Thread (Priority 2) より高い優先度で動作するため、カメラキャプチャ処理がGUI描画よりも優先されます。これはリアルタイムなフレームキャプチャを確保するための設計判断です。

> **注意**: Issue #14の「5. 既存スレッドとの優先度整理」表では、blinky_thread=1, ntshell_thread=2, lvgl_thread=3, camera_thread=4 と記載されていますが、現在のconfiguration.xmlのntshell_thread(Priority 1)やlvgl_thread(Priority 2)は変更対象に含まれていません。本手順書ではcamera_threadのPriorityのみを変更します。ntshell_threadやlvgl_threadの優先度変更が必要な場合は、別途Issueを作成してください。

---

## 手順3: スタック構成の確認

Stacksタブで、Camera Thread配下の構成が以下のとおり維持されていることを確認してください（Issue #10で追加済み、変更不要）:

```
Camera Thread (camera_thread)  -- Priority: 4, Stack: 4096
  +-- g_vin0 Video Input (VIN) (r_vin)
        +-- g_mipi_csi0 MIPI CSI-2 (r_mipi_csi)
              +-- g_mipi_phy0 MIPI PHY Host (r_mipi_phy)
```

---

## 最終手順: コード生成とビルド確認

### コード生成

1. `e2studio_CPU0/configuration.xml` のエディタ上部にある **Generate Project Content** ボタンをクリック
2. コード生成が完了するまで待つ
3. エラーが表示されないことを確認

### 生成されるファイルの確認

スレッドのプロパティ変更により、以下のファイルが更新されます:

| ファイル | 更新内容 |
|---------|---------|
| `ra_gen/camera_thread.c` | スタックサイズ（10240 -> 4096）、優先度（3 -> 4）の反映 |
| `ra_gen/camera_thread.h` | スレッド関連定義の更新 |

> **注意**: `ra_gen/` 配下のファイルは自動生成されるため、手動で編集しないでください。

### ビルド確認

1. **Project** > **Build Project** を実行
2. ビルドがエラーなく完了することを確認

### 既存のエントリ関数ファイル

`e2studio_CPU0/src/camera_thread_entry.c` は既に存在しています。Issue #14の実装内容（OV5640初期化、VINキャプチャ開始等）はこのファイルに追記します。FSP設定手順としてはここまでで完了です。

---

## 受け入れ確認チェックリスト

- [ ] Camera ThreadのStack sizeが4096に変更されている
- [ ] Camera ThreadのPriorityが4に変更されている
- [ ] Camera Thread配下のスタック構成（VIN > MIPI CSI > MIPI PHY）が維持されている
- [ ] FreeRTOS configMAX_PRIORITIESが5以上であること（現在5、変更不要）
- [ ] Generate Project Contentでエラーなくコード生成が完了する
- [ ] ビルドがエラーなく完了する

---

## 参照情報

| 項目 | 参照先 |
|------|--------|
| リファレンスプロジェクト | `reference_projects/quickstart_ek_ra8p1_ep/e2studio/configuration.xml` |
| リファレンスのcamera_thread設定 | リファレンス configuration.xml L2045-L2052 |
| 現プロジェクトのcamera_thread設定 | `e2studio_CPU0/configuration.xml` L956-L969 |
| 前提手順書（Issue #10） | `doc/fsp-setup-guide/issue-10-vin-mipi-csi-phy.md` |
| Issue | https://github.com/grace2riku/mimamori-sense/issues/14 |
