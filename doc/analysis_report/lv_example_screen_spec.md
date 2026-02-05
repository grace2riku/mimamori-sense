# LVGL サンプルアプリケーション 画面仕様書

**作成日**: 2026-02-05
**対象プロジェクト**: `reference_projects/lv_port_renesas_ek_ra8p1`
**ディスプレイ解像度**: 1024 x 600 ピクセル (RGB565)

---

## 1. 画面構成概要

本サンプルアプリケーションは「Settings（設定）画面」のみで構成されています。

```
┌─────────────────────────────────────────────────────────────────┐
│ [Settings]                    [🔔][📶][📡]  [17:45]            │ ← Header (32px)
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ☐ Notifications                          ┌───────┐ ┌───────┐ │
│                                           │ Hour  │ │ Mins  │ │
│  ☐ Bluetooth                              │┌─────┐│ │┌─────┐│ │
│                                           ││ 17  ││ ││ 45  ││ │
│  ☐ WiFi                                   │└─────┘│ │└─────┘│ │
│                                           └───────┘ └───────┘ │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 2. 画面仕様

### 2.1 Settings画面（メイン画面）

| 項目 | 値 |
|------|-----|
| **作成関数** | `settings_create()` |
| **サイズ** | 幅: 100% (1024px), 高さ: 100% (600px) |
| **座標** | (0, 0) - 親なし（ルートスクリーン） |
| **レイアウト** | Flexbox (COLUMN) |
| **フォント** | font_subtitle (Inter SemiBold 14px) |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| padding (all) | 0 |
| border_width | 0 |
| radius | 0 |
| flex_flow | LV_FLEX_FLOW_COLUMN |
| pad_row | 0 |

---

## 3. GUI部品仕様

### 3.1 Header コンポーネント

**作成関数**: `header_create(parent, "Settings")`

| 項目 | 値 |
|------|-----|
| **サイズ** | 幅: 100%, 高さ: 32px |
| **座標** | 親のFlexboxにより自動配置（上部） |
| **背景色** | #2d2d2d (ダークグレー) |
| **テキスト色** | #ffffff (白) |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| bg_color | #2d2d2d |
| border_width | 2px |
| border_side | LV_BORDER_SIDE_BOTTOM |
| border_color | #a2a2a2 |
| radius | 0 |
| pad_hor | 8px |
| layout | LV_LAYOUT_FLEX |
| flex_flow | LV_FLEX_FLOW_ROW |
| flex_cross_place | LV_FLEX_ALIGN_CENTER |
| flex_track_place | LV_FLEX_ALIGN_CENTER |
| scrollable | false |

#### Header 子要素

| No. | 部品 | プロパティ | 初期値/設定 |
|-----|------|-----------|------------|
| 1 | subtitle_no_bind (タイトル) | flex_grow: 1 | text: "Settings" |
| 2 | icon (通知) | 14x14px, recolor: #ffffff | src: img_bell, DISABLED時 opacity: 128 |
| 3 | icon (Bluetooth) | 14x14px, recolor: #ffffff | src: img_bluetooth, DISABLED時 opacity: 128 |
| 4 | icon (WiFi) | 14x14px, recolor: #ffffff | src: img_wifi, DISABLED時 opacity: 128 |
| 5 | row (時刻表示) | 幅: 40px | 時:分 表示 |

##### 時刻表示 Row の子要素

| No. | 部品 | バインド | 表示形式 |
|-----|------|---------|---------|
| 5-1 | subtitle | subject_hours | "%02d" |
| 5-2 | subtitle_no_bind | - | ":" |
| 5-3 | subtitle | subject_mins | "%02d" |

---

### 3.2 Content Row コンポーネント

**作成関数**: `row_create(parent)` + カスタムスタイル

| 項目 | 値 |
|------|-----|
| **サイズ** | 幅: 100%, 高さ: 自動伸長 (flex_grow: 1) |
| **座標** | 親のFlexboxにより自動配置（Header下） |
| **背景色** | #ebebeb (ライトグレー) |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| bg_color | #ebebeb |
| bg_opa | 255 (不透明) |
| pad_all | 12px |
| flex_grow | 1 |
| flex_flow | LV_FLEX_FLOW_ROW_WRAP |

#### Content Row 子要素

| No. | 部品 | 内容 |
|-----|------|------|
| 1 | column (チェックボックス列) | Notifications, Bluetooth, WiFi |
| 2 | setclock (時刻設定) | Hour/Mins ローラー |

---

### 3.3 Checkbox コンポーネント

**作成関数**: `checkbox_create(parent, text, subject)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_checkbox |
| **サイズ** | 自動（テキスト依存） |

#### インスタンス一覧

| No. | テキスト | バインド対象 | 初期値 |
|-----|---------|-------------|--------|
| 1 | "Notifications" | subject_notification_on | 0 (未チェック) |
| 2 | "Bluetooth" | subject_bluetooth_on | 0 (未チェック) |
| 3 | "WiFi" | subject_wifi_on | 0 (未チェック) |

#### スタイルプロパティ (INDICATOR部分, PRESSED状態)

| プロパティ | 値 |
|-----------|-----|
| transform_height | 0 |
| transform_width | 0 |

#### イベントハンドラ

- **バインディング**: `lv_obj_bind_checked(checkbox, subject)`
- **動作**: チェック状態が変更されると、バインドされたsubjectの値が自動更新される
  - チェックON → subject値 = 1
  - チェックOFF → subject値 = 0

---

### 3.4 Setclock コンポーネント

**作成関数**: `setclock_create(parent)`

| 項目 | 値 |
|------|-----|
| **サイズ** | 幅: LV_SIZE_CONTENT, 高さ: LV_SIZE_CONTENT |
| **座標** | 親のFlexboxにより自動配置 |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| flex_flow | LV_FLEX_FLOW_ROW |
| pad_all | 0 |
| bg_opa | 0 (透明) |
| border_width | 0 |

#### Setclock 子要素

##### Column 1 (Hour)

| 項目 | 値 |
|------|-----|
| flex_cross_place | LV_FLEX_ALIGN_CENTER |

| No. | 部品 | 設定 |
|-----|------|------|
| 1 | subtitle_no_bind | text: "Hour" |
| 2 | roller | options: 0-24, バインド: subject_hours |

##### Column 2 (Mins)

| 項目 | 値 |
|------|-----|
| flex_cross_place | LV_FLEX_ALIGN_CENTER |

| No. | 部品 | 設定 |
|-----|------|------|
| 1 | subtitle_no_bind | text: "Mins" |
| 2 | roller | options: 0-60, バインド: subject_mins |

---

### 3.5 Roller（時刻選択）

**LVGLウィジェット**: lv_roller

#### Hour Roller

| 項目 | 値 |
|------|-----|
| **オプション** | "0\n1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n12\n13\n14\n15\n16\n17\n18\n19\n20\n21\n22\n23\n24" |
| **モード** | LV_ROLLER_MODE_NORMAL |
| **バインド** | subject_hours |
| **初期値** | 17 |

#### Mins Roller

| 項目 | 値 |
|------|-----|
| **オプション** | "0\n1\n2\n...\n59\n60" (0-60の連番) |
| **モード** | LV_ROLLER_MODE_NORMAL |
| **バインド** | subject_mins |
| **初期値** | 45 |

#### イベントハンドラ

- **バインディング**: `lv_roller_bind_value(roller, subject)`
- **動作**: ローラーの値が変更されると、バインドされたsubjectの値が自動更新される

---

### 3.6 Icon コンポーネント

**作成関数**: `icon_create(parent, src)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_image |
| **サイズ** | 幅: 14px, 高さ: 14px |
| **画像配置** | LV_IMAGE_ALIGN_STRETCH |

#### スタイルプロパティ

| 状態 | プロパティ | 値 |
|------|-----------|-----|
| DEFAULT | image_recolor_opa | 255 |
| DEFAULT | image_recolor | #ffffff |
| DISABLED | image_recolor_opa | 128 |
| DISABLED | image_recolor | #ffffff |

#### インスタンス一覧

| アイコン | 画像ソース | 状態バインド |
|---------|-----------|-------------|
| 通知 | img_bell | subject_notification_on == 0 → DISABLED |
| Bluetooth | img_bluetooth | subject_bluetooth_on == 0 → DISABLED |
| WiFi | img_wifi | subject_wifi_on == 0 → DISABLED |

#### イベントハンドラ

- **バインディング**: `lv_obj_bind_state_if_eq(icon, subject, LV_STATE_DISABLED, 0)`
- **動作**: subjectの値が0の場合、アイコンがDISABLED状態になり、半透明表示(opacity: 128)になる

---

### 3.7 Row コンポーネント

**作成関数**: `row_create(parent)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_obj |
| **サイズ** | 幅: LV_SIZE_CONTENT, 高さ: LV_SIZE_CONTENT |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| bg_opa | 0 (透明) |
| border_width | 0 |
| pad_all | 0 |
| layout | LV_LAYOUT_FLEX |
| flex_flow | LV_FLEX_FLOW_ROW |
| radius | 0 |

---

### 3.8 Column コンポーネント

**作成関数**: `column_create(parent)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_obj |
| **サイズ** | 幅: LV_SIZE_CONTENT, 高さ: LV_SIZE_CONTENT |

#### スタイルプロパティ

| プロパティ | 値 |
|-----------|-----|
| bg_opa | 0 (透明) |
| border_width | 0 |
| pad_all | 0 |
| layout | LV_LAYOUT_FLEX |
| flex_flow | LV_FLEX_FLOW_COLUMN |
| radius | 0 |

---

### 3.9 Subtitle コンポーネント（バインドあり）

**作成関数**: `subtitle_create(parent, text, bind_text)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_label |
| **フォント** | font_subtitle (Inter SemiBold 14px) |
| **表示形式** | "%02d" (2桁ゼロ埋め) |

#### イベントハンドラ

- **バインディング**: `lv_label_bind_text(label, subject, "%02d")`
- **動作**: subjectの値が変更されると、自動的にラベルテキストが更新される

---

### 3.10 Subtitle No Bind コンポーネント（バインドなし）

**作成関数**: `subtitle_no_bind_create(parent, text)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_label |
| **フォント** | font_subtitle (Inter SemiBold 14px) |

---

### 3.11 Title コンポーネント

**作成関数**: `title_create(parent, text)`

| 項目 | 値 |
|------|-----|
| **LVGLウィジェット** | lv_label |
| **フォント** | font_title (Inter SemiBold 20px) |

※ 本サンプルでは未使用

---

## 4. 状態管理（Subject）

本アプリケーションはLVGLのObserver機能を使用して状態を管理しています。

### 4.1 Subject一覧

| Subject名 | 型 | 初期値 | 用途 |
|-----------|-----|--------|------|
| subject_hours | int | 17 | 時刻（時） |
| subject_mins | int | 45 | 時刻（分） |
| subject_bluetooth_on | int | 0 | Bluetooth ON/OFF状態 |
| subject_wifi_on | int | 0 | WiFi ON/OFF状態 |
| subject_notification_on | int | 0 | 通知 ON/OFF状態 |

### 4.2 Subject初期化コード

```c
void ui_init_gen(const char * asset_path)
{
    lv_subject_init_int(&subject_hours, 17);
    lv_subject_init_int(&subject_mins, 45);
    lv_subject_init_int(&subject_bluetooth_on, 0);
    lv_subject_init_int(&subject_wifi_on, 0);
    lv_subject_init_int(&subject_notification_on, 0);
}
```

---

## 5. フォント・画像リソース

### 5.1 フォント

| フォント名 | ソース | サイズ |
|-----------|--------|--------|
| font_title | Inter_SemiBold_ttf_data | 20px |
| font_subtitle | Inter_SemiBold_ttf_data | 14px |

### 5.2 画像

| 画像名 | ファイル | 用途 |
|--------|---------|------|
| img_wifi | wifi-solid.png | WiFiアイコン |
| img_bluetooth | bluetooth-brands.png | Bluetoothアイコン |
| img_bell | bell-solid.png | 通知アイコン |

---

## 6. 画面遷移

### 6.1 遷移図

```
┌──────────────────┐
│  Settings画面    │
│  (唯一の画面)    │
└──────────────────┘
```

本サンプルアプリケーションは単一画面構成のため、画面遷移はありません。

### 6.2 画面読み込みコード

```c
// new_thread0_entry.c
void new_thread0_entry(void *pvParameters)
{
    // ...初期化処理...

    ui_init(NULL);  // UI初期化

    settings = settings_create();  // Settings画面作成
    lv_screen_load(settings);      // 画面表示

    while (1) {
        lv_timer_handler();  // LVGLメインループ
        vTaskDelay(1);
    }
}
```

---

## 7. イベントハンドラ一覧

### 7.1 バインディングによる自動イベント処理

| 対象部品 | バインディング関数 | 動作 |
|---------|-------------------|------|
| Checkbox (Notifications) | `lv_obj_bind_checked()` | チェック変更 → subject_notification_on更新 |
| Checkbox (Bluetooth) | `lv_obj_bind_checked()` | チェック変更 → subject_bluetooth_on更新 |
| Checkbox (WiFi) | `lv_obj_bind_checked()` | チェック変更 → subject_wifi_on更新 |
| Roller (Hour) | `lv_roller_bind_value()` | 値変更 → subject_hours更新 |
| Roller (Mins) | `lv_roller_bind_value()` | 値変更 → subject_mins更新 |
| Icon (通知) | `lv_obj_bind_state_if_eq()` | subject=0 → DISABLED状態 |
| Icon (Bluetooth) | `lv_obj_bind_state_if_eq()` | subject=0 → DISABLED状態 |
| Icon (WiFi) | `lv_obj_bind_state_if_eq()` | subject=0 → DISABLED状態 |
| Label (時刻表示) | `lv_label_bind_text()` | subject変更 → テキスト更新 |

### 7.2 データフロー

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│  Checkbox   │────→│   Subject    │────→│   Header    │
│  (入力)     │     │  (状態管理)  │     │  Icon (表示)│
└─────────────┘     └──────────────┘     └─────────────┘
                           │
┌─────────────┐            │              ┌─────────────┐
│   Roller    │────────────┴─────────────→│   Label     │
│  (入力)     │                           │  (時刻表示) │
└─────────────┘                           └─────────────┘
```

---

## 8. 座標・サイズ詳細

### 8.1 絶対座標マップ（概算値）

```
(0,0)                                              (1024,0)
  ┌──────────────────────────────────────────────────┐
  │ Header                                           │
  │ Y: 0-32px                                        │
  ├──────────────────────────────────────────────────┤ (0,32)
  │                                                  │
  │ Content Area                                     │
  │ Y: 32-600px                                      │
  │                                                  │
  │  ┌────────────────┐     ┌──────────────────────┐│
  │  │ Column         │     │ Setclock             ││
  │  │ (Checkboxes)   │     │ (Hour/Mins Rollers)  ││
  │  │                │     │                      ││
  │  └────────────────┘     └──────────────────────┘│
  │                                                  │
  └──────────────────────────────────────────────────┘
(0,600)                                          (1024,600)
```

### 8.2 レイアウト計算

全てのコンポーネントはFlexboxレイアウトを使用しており、座標は自動計算されます。

| レイアウト | 親 → 子 |
|-----------|---------|
| Settings画面 | COLUMN → Header, Content Row |
| Header | ROW → タイトル, アイコン×3, 時刻Row |
| Content Row | ROW_WRAP → Column(チェックボックス), Setclock |
| Column | COLUMN → Checkbox×3 |
| Setclock | ROW → Column(Hour), Column(Mins) |

---

## 付録A: ソースファイル参照

| ファイル | 役割 |
|---------|------|
| `ui/ui_gen.c` | グローバルリソース初期化（フォント、画像、Subject） |
| `ui/ui_gen.h` | グローバル宣言 |
| `ui/screens/settings/settings_gen.c` | Settings画面作成 |
| `ui/components/header/header_gen.c` | Headerコンポーネント |
| `ui/components/checkbox/checkbox_gen.c` | Checkboxコンポーネント |
| `ui/components/setclock/setclock_gen.c` | Setclockコンポーネント |
| `ui/components/row/row_gen.c` | Rowコンポーネント |
| `ui/components/column/column_gen.c` | Columnコンポーネント |
| `ui/components/icon/icon_gen.c` | Iconコンポーネント |
| `ui/components/subtitle/subtitle_gen.c` | Subtitle（バインドあり）コンポーネント |
| `ui/components/subtiltle_no_bind/subtitle_no_bind_gen.c` | Subtitle（バインドなし）コンポーネント |
| `ui/components/title/title_gen.c` | Titleコンポーネント |

---

## 付録B: 追加Q&A

### Q1. デモ画面切り替えマクロについて

**質問**: lv_port_renesas_ek_ra8p1をビルドし書き込むとLV_USE_DEMO_BENCHMARKマクロが有効になり、「Settings（設定）画面」とは違う画面が表示されます。マクロの種類と設定、表示される画面を教えてください。

**回答**:

本アプリケーションは`src/new_thread0_entry.c`および`src/lv_conf_user.h`のマクロ設定により、表示する画面を切り替えられる構造になっています。

#### マクロ設定と表示画面一覧

| 優先度 | マクロ名 | 設定ファイル | 呼び出し関数 | 表示内容 |
|--------|---------|-------------|-------------|---------|
| 1 | `LV_USE_DEMO_BENCHMARK` | lv_conf_user.h | `lv_demo_benchmark()` | パフォーマンスベンチマーク画面 |
| 2 | `LV_USE_DEMO_MUSIC` | lv_conf.h | `lv_demo_music()` | 音楽プレーヤーデモ画面 |
| 3 | `LV_USE_DEMO_KEYPAD_AND_ENCODER` | lv_conf.h | `lv_demo_keypad_encoder()` | キーパッド/エンコーダーデモ |
| 4 | `LV_USE_DEMO_STRESS` | lv_conf.h | `lv_demo_stress()` | ストレステスト画面 |
| 5 | `LV_USE_DEMO_WIDGETS` | lv_conf_user.h | `lv_demo_widgets()` | ウィジェットデモ画面 |
| 6 | `USE_LVGL_EDITOR` | 自動定義 | `settings_create()` | Settings（設定）画面 |

#### 現在のデフォルト設定（lv_conf_user.h）

```c
#define LV_BUILD_DEMOS 1

#if LV_BUILD_DEMOS
    #define LV_USE_DEMO_WIDGETS 1
    #define LV_USE_DEMO_BENCHMARK 1
#endif
```

現在の設定では`LV_USE_DEMO_BENCHMARK = 1`が有効なため、ベンチマーク画面が表示されます。

#### Settings画面を表示するための設定変更

Settings画面を表示するには、`lv_conf_user.h`を以下のように変更します:

```c
#define LV_BUILD_DEMOS 0  // デモを無効化

// または以下のように個別に無効化
#define LV_USE_DEMO_WIDGETS 0
#define LV_USE_DEMO_BENCHMARK 0
```

#### 画面選択ロジック（new_thread0_entry.c）

```c
// USE_LVGL_EDITORは全デモが無効の場合に自動定義される
#if (0 == LV_USE_DEMO_BENCHMARK) && (0 == LV_USE_DEMO_MUSIC) && \
    (0 == LV_USE_DEMO_KEYPAD_AND_ENCODER) && (0 == LV_USE_DEMO_WIDGETS)
#define USE_LVGL_EDITOR 1
#endif

// 画面選択の優先順位
#if (1 == LV_USE_DEMO_BENCHMARK)
    lv_demo_benchmark();
#elif (1 == LV_USE_DEMO_MUSIC)
    lv_demo_music();
#elif (1 == LV_USE_DEMO_KEYPAD_AND_ENCODER)
    lv_demo_keypad_encoder();
#elif (1 == LV_USE_DEMO_STRESS)
    lv_demo_stress();
#elif (1 == LV_USE_DEMO_WIDGETS && 0 == LV_USE_DEMO_BENCHMARK)
    lv_demo_widgets();
#elif (1 == USE_LVGL_EDITOR)
    // Settings画面を表示
    ui_init(NULL);
    settings = settings_create();
    lv_screen_load(settings);
#endif
```

---

### Q2. Flexboxレイアウトについて

**質問**: Flexboxレイアウトについて解説してください。Flexbox以外にもレイアウト方式があるのか教えてください。

**回答**:

#### Flexboxレイアウトとは

Flexbox（フレックスボックス）は、CSSの概念を元にした1次元レイアウトシステムです。子要素を**行（ROW）** または **列（COLUMN）** の方向に自動配置します。

#### LVGLでのFlexboxレイアウト設定

```c
// Flexboxレイアウトを有効化
lv_obj_set_layout(obj, LV_LAYOUT_FLEX);

// 配置方向を設定
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);     // 横並び
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN);  // 縦並び
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW_WRAP);    // 横並び（折り返しあり）
lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_COLUMN_WRAP); // 縦並び（折り返しあり）
```

#### Flexboxの主要プロパティ

| プロパティ | 説明 | 設定例 |
|-----------|------|--------|
| `flex_flow` | 子要素の配置方向 | ROW, COLUMN, ROW_WRAP, COLUMN_WRAP |
| `flex_grow` | 余白の拡張割合 | 0（固定）, 1以上（拡張） |
| `flex_main_place` | 主軸方向の配置 | START, END, CENTER, SPACE_EVENLY, SPACE_AROUND, SPACE_BETWEEN |
| `flex_cross_place` | 交差軸方向の配置 | START, END, CENTER |
| `flex_track_place` | 複数行時のトラック配置 | START, END, CENTER, SPACE_EVENLY, SPACE_AROUND, SPACE_BETWEEN |
| `pad_row` | 行間の余白 | ピクセル値 |
| `pad_column` | 列間の余白 | ピクセル値 |

#### Flexboxの配置例

```
┌─────────────────────────────────────┐
│ LV_FLEX_FLOW_ROW                    │
│ ┌───┐ ┌───┐ ┌───┐                  │
│ │ A │ │ B │ │ C │  ← 横並び        │
│ └───┘ └───┘ └───┘                  │
└─────────────────────────────────────┘

┌─────────────────────────────────────┐
│ LV_FLEX_FLOW_COLUMN                 │
│ ┌───┐                               │
│ │ A │  ← 縦並び                     │
│ └───┘                               │
│ ┌───┐                               │
│ │ B │                               │
│ └───┘                               │
│ ┌───┐                               │
│ │ C │                               │
│ └───┘                               │
└─────────────────────────────────────┘
```

#### LVGLで使用可能なレイアウト方式

LVGLでは以下の3種類のレイアウト方式が利用可能です:

| レイアウト | 定数 | 説明 | 適用例 |
|-----------|------|------|--------|
| **None (手動配置)** | `LV_LAYOUT_NONE` | 座標を直接指定 | 固定位置のUI、ゲーム画面 |
| **Flexbox** | `LV_LAYOUT_FLEX` | 1次元配置（行/列） | リスト、メニュー、ツールバー |
| **Grid** | `LV_LAYOUT_GRID` | 2次元配置（行列） | ダッシュボード、カレンダー |

#### Gridレイアウトの概要

Gridは2次元のレイアウトシステムで、行と列を定義してセルに子要素を配置します。

```c
// Gridレイアウト設定例
static int32_t col_dsc[] = {100, 100, LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
static int32_t row_dsc[] = {50, 50, 50, LV_GRID_TEMPLATE_LAST};

lv_obj_set_layout(parent, LV_LAYOUT_GRID);
lv_obj_set_grid_dsc_array(parent, col_dsc, row_dsc);

// 子要素をセルに配置
lv_obj_set_grid_cell(child, LV_GRID_ALIGN_CENTER, 0, 1,  // 列0, 幅1
                            LV_GRID_ALIGN_CENTER, 0, 1); // 行0, 高さ1
```

```
┌─────────────────────────────────────┐
│ Grid Layout (3列 × 3行)             │
│ ┌─────┬─────┬───────────┐          │
│ │(0,0)│(1,0)│   (2,0)   │          │
│ ├─────┼─────┼───────────┤          │
│ │(0,1)│(1,1)│   (2,1)   │          │
│ ├─────┼─────┼───────────┤          │
│ │(0,2)│(1,2)│   (2,2)   │          │
│ └─────┴─────┴───────────┘          │
└─────────────────────────────────────┘
```

#### レイアウト方式の選択指針

| 用途 | 推奨レイアウト |
|------|---------------|
| 単純な縦/横並び | Flexbox |
| リスト表示 | Flexbox (COLUMN) |
| ツールバー/メニューバー | Flexbox (ROW) |
| ダッシュボード（タイル配置） | Grid |
| カレンダー/表形式 | Grid |
| 絶対位置が必要な場合 | None (手動配置) |
| ゲームUI | None (手動配置) |

---

### Q3. チェックボックスのサイズ変更について

**質問**: チェックボックスが小さすぎてチェックしづらかったです。3倍ほどの大きさに変更することは可能ですか？他のGUI部品も大きさは変更可能ですか？

**回答**:

#### チェックボックスのサイズ変更 - 可能です

LVGLのチェックボックスは、スタイルを適用することでサイズを変更できます。

#### チェックボックスの構造

```
┌─────────────────────────────────────┐
│ lv_checkbox                         │
│ ┌──────────┐                        │
│ │ INDICATOR│  ← チェックマーク部分  │
│ │  (□/☑)  │                        │
│ └──────────┘                        │
│  Notifications  ← MAIN部分（テキスト）│
└─────────────────────────────────────┘
```

#### サイズを3倍にする実装例

```c
// スタイル定義
static lv_style_t style_checkbox_large;

void init_checkbox_style(void)
{
    lv_style_init(&style_checkbox_large);

    // インジケーター（チェックボックス部分）のサイズを3倍に
    // デフォルトは約18px → 54pxに変更
    lv_style_set_width(&style_checkbox_large, 54);
    lv_style_set_height(&style_checkbox_large, 54);

    // 角丸も調整
    lv_style_set_radius(&style_checkbox_large, 8);

    // ボーダー幅も調整
    lv_style_set_border_width(&style_checkbox_large, 3);
}

// チェックボックスにスタイル適用
lv_obj_t * checkbox = lv_checkbox_create(parent);
lv_checkbox_set_text(checkbox, "Notifications");

// INDICATOR部分にスタイルを適用
lv_obj_add_style(checkbox, &style_checkbox_large, LV_PART_INDICATOR);
```

#### 既存コードの修正箇所

`ui/components/checkbox/checkbox_gen.c`を以下のように修正:

```c
lv_obj_t * checkbox_create(lv_obj_t * parent, const char * text, lv_subject_t * subject)
{
    static lv_style_t style_box;
    static lv_style_t style_indicator_large;  // 追加

    static bool style_inited = false;

    if (!style_inited) {
        lv_style_init(&style_box);
        lv_style_set_transform_height(&style_box, 0);
        lv_style_set_transform_width(&style_box, 0);

        // 大きいインジケータースタイルを追加
        lv_style_init(&style_indicator_large);
        lv_style_set_width(&style_indicator_large, 54);   // 3倍サイズ
        lv_style_set_height(&style_indicator_large, 54);  // 3倍サイズ
        lv_style_set_radius(&style_indicator_large, 8);
        lv_style_set_border_width(&style_indicator_large, 3);

        style_inited = true;
    }

    lv_obj_t * lv_checkbox_1 = lv_checkbox_create(parent);
    lv_obj_add_style(lv_checkbox_1, &style_box, LV_PART_INDICATOR | LV_STATE_PRESSED);
    lv_obj_add_style(lv_checkbox_1, &style_indicator_large, LV_PART_INDICATOR);  // 追加

    lv_obj_bind_checked(lv_checkbox_1, subject);
    lv_checkbox_set_text(lv_checkbox_1, text);

    return lv_checkbox_1;
}
```

#### 他のGUI部品のサイズ変更 - すべて可能です

LVGLの全てのウィジェットはスタイルでサイズ変更が可能です。

| ウィジェット | サイズ変更方法 | 対象パーツ |
|-------------|---------------|-----------|
| **Checkbox** | スタイルで `width`, `height` 設定 | `LV_PART_INDICATOR` |
| **Switch** | スタイルで `width`, `height` 設定 | `LV_PART_MAIN`, `LV_PART_INDICATOR` |
| **Slider** | `lv_obj_set_size()` または スタイル | `LV_PART_MAIN`, `LV_PART_INDICATOR`, `LV_PART_KNOB` |
| **Button** | `lv_obj_set_size()` | `LV_PART_MAIN` |
| **Roller** | `lv_obj_set_size()` | `LV_PART_MAIN`, `LV_PART_SELECTED` |

#### Switch（スイッチ）のサイズ変更例

```c
// スイッチを作成
lv_obj_t * sw = lv_switch_create(parent);

// 直接サイズ設定（幅100px, 高さ50px）
lv_obj_set_size(sw, 100, 50);

// または、スタイルで設定
static lv_style_t style_switch;
lv_style_init(&style_switch);
lv_style_set_width(&style_switch, 100);
lv_style_set_height(&style_switch, 50);
lv_obj_add_style(sw, &style_switch, LV_PART_MAIN);
```

#### Slider（スライダー）のサイズ変更例

```c
lv_obj_t * slider = lv_slider_create(parent);

// スライダー全体のサイズ
lv_obj_set_size(slider, 300, 30);

// つまみ（ノブ）のサイズ変更
static lv_style_t style_knob;
lv_style_init(&style_knob);
lv_style_set_width(&style_knob, 40);
lv_style_set_height(&style_knob, 40);
lv_style_set_radius(&style_knob, 20);  // 円形にする
lv_obj_add_style(slider, &style_knob, LV_PART_KNOB);
```

#### タッチしやすいUIのための推奨サイズ

| 部品 | 推奨最小サイズ | 理由 |
|------|--------------|------|
| ボタン | 48×48 px | タッチターゲット推奨サイズ |
| チェックボックス | 44×44 px | 指で操作しやすいサイズ |
| スイッチ | 60×30 px | ON/OFF状態が見やすい |
| スライダーのノブ | 40×40 px | つまみやすいサイズ |

---

*このドキュメントはClaude Codeにより自動生成されました*
