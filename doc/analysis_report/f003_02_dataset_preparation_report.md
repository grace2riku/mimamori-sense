# F-003-2: 転倒検出AIモデルの学習データセット準備レポート

本レポートはIssue #19 (F-003-2)の成果物である。F-003-1で選定したモデル (YOLO-Fastest v1.1) およびアプローチ (人物検出 + アスペクト比判定) に基づき、学習データセットの準備方針をまとめる。

---

## 1. 前提条件

### 1.1 F-003-1 選定結果の要約

| 項目 | 値 |
|---|---|
| 選定モデル | YOLO-Fastest v1.1 |
| 入力サイズ | 192x192x1 (Grayscale, INT8) |
| 検出クラス数 | 1 (person) |
| 転倒判定方式 | アプローチC: 人物検出 + バウンディングボックスのアスペクト比判定 |
| 学習フレームワーク | Darknet |
| アノテーション形式 | YOLO Darknet TXT形式 |

### 1.2 データセットに求められる要件

| # | 要件 | 根拠 |
|---|---|---|
| 1 | 人物 (person) の1クラスバウンディングボックスアノテーション | アプローチC: 人物検出モデルの学習に必要 |
| 2 | 1,000枚以上の画像を提案 | Issue #19 受け入れ条件 |
| 3 | 立位・座位・歩行・転倒等の多様な姿勢を含む | 転倒判定の精度向上に必要 |
| 4 | 室内環境の画像を含む | 家庭内見守り端末の使用環境に合致 |
| 5 | 3種類以上の転倒シーンバリエーション | Issue #19 受け入れ条件 |
| 6 | Grayscale変換後も人物検出可能な画質 | 192x192x1 Grayscale入力に対応 |

### 1.3 カメラ仕様

| 項目 | 値 |
|---|---|
| カメラモジュール | OV5640 MIPI CSI-2 |
| キャプチャ解像度 | 320x240 RGB565 |
| AI入力への変換 | RGB565 -> Grayscale INT8 (192x192) |
| Grayscale変換式 | gray = 0.299*R + 0.587*G + 0.114*B - 128 (ITU-R BT.601) |

---

## 2. 公開データセットの調査

### 2.1 データセット比較表

| # | データセット名 | 画像数 (概算) | アノテーション形式 | クラス | 転倒シーン | 入手性 | 適用性 |
|---|---|---|---|---|---|---|---|
| 1 | **COCO 2017 (person)** | 約64,000枚 (personを含む画像) | COCO JSON (BBox) | 80クラス中personを抽出 | 立位・歩行が中心 | 高 (公開) | **高** |
| 2 | **Roboflow Fall Detection** | 約2,800枚 | YOLO TXT / COCO JSON等 | fall / not-fall の2クラス | 転倒シーン含む | 高 (Roboflow公開) | **高** |
| 3 | **UR Fall Detection (URFD)** | 約70動画 (約30,000フレーム) | 動画+アクティビティラベル | fall / ADL | 転倒5種+日常動作 | 中 (研究用公開) | **高** |
| 4 | **Le2i Fall Detection** | 約220動画 | 動画+フレームラベル | fall / confounding | 室内転倒シーン | 中 (申請必要な場合あり) | 高 |
| 5 | **DiverseFall10500** | 10,500枚 | YOLO TXT | fall / not-fall | 多様な転倒パターン | 中 (Mendeley Data) | 高 |
| 6 | **Kaggle Fall Detection (各種)** | 数百~数千枚 | CSV / YOLO TXT等 | fall / not-fall | 転倒シーン含む | 高 (Kaggle公開) | 中 |
| 7 | **Wake Vision** | 112,000枚 | TFRecord / 画像分類 | person / no-person | 人物有無のみ | 高 (TensorFlow公開) | 中 (分類用) |
| 8 | **GMDCSA-24** | 44,000+枚 | YOLO TXT | fall / not-fall | 多様な転倒パターン | 中 (Mendeley Data) | 高 |
| 9 | **CAUCAFall** | 動画ベース | 動画+アクティビティラベル | fall / ADL | 高齢者転倒シミュレーション | 低 (申請必要) | 中 |

### 2.2 各データセットの詳細

#### 2.2.1 COCO 2017 (person クラス抽出)

**概要:** Microsoft COCOは物体検出の標準的なベンチマークデータセットである。80クラス中の "person" クラスのみを抽出して使用する。

- **URL:** https://cocodataset.org/
- **train2017:** 約64,000枚がpersonクラスを含む (全118,287枚中)
- **val2017:** 約2,700枚がpersonクラスを含む (全5,000枚中)
- **アノテーション:** COCO JSON形式 (BBox: x, y, width, height)
- **ライセンス:** Creative Commons Attribution 4.0

**利点:**
- 大規模かつ高品質なアノテーション
- 人物の多様な姿勢 (立位、座位、歩行、スポーツ等) を含む
- 物体検出の学習データとして十分検証されている
- pycocotoolsライブラリでpersonクラスのみ容易に抽出可能

**制限事項:**
- 転倒状態の人物は少ない (日常シーンが中心)
- 室外シーンが多く、室内環境に偏りがない
- 全画像をダウンロードすると約20GB (train2017)

**推奨使用方法:** train2017から5,000-10,000枚を抽出し、ベース学習データとして使用する。立位・歩行・座位の "non-fall" 正常姿勢の学習に最適。

#### 2.2.2 Roboflow Fall Detection Dataset

**概要:** Roboflow Universe上で公開されている転倒検出用データセット。複数のバリエーションがあるが、代表的なものを取り上げる。

- **URL:** https://universe.roboflow.com/ で "fall detection" を検索
- **代表的プロジェクト:**
  - Fall Detection (YOLO形式対応、約1,000-2,800枚)
  - Fall Detection Computer Vision Project (YOLO/COCO/VOC等複数形式に対応)
- **アノテーション:** YOLO TXT / COCO JSON / Pascal VOC等、エクスポート時に選択可能
- **クラス:** fall / not-fall (または fall / standing 等)
- **ライセンス:** プロジェクトにより異なる (CC BY 4.0が多い)

**利点:**
- YOLO形式で直接ダウンロード可能 (変換作業不要)
- 転倒シーンのアノテーション済みデータが含まれる
- Roboflowのデータ拡張ツールと連携可能
- 複数のバリエーションから目的に合うものを選択できる

**制限事項:**
- データ品質にばらつきがある (ユーザー投稿型のため)
- クラス定義が "fall/not-fall" の2クラスであり、本プロジェクトの "person" 1クラスに統合する変換が必要
- 一部データセットはダウンロードにRoboflowアカウントが必要

**推奨使用方法:** fall/not-fall の2クラスを "person" 1クラスに統合して使用する。転倒シーンの人物バウンディングボックスとして貴重なデータソース。1,000-2,800枚を全量使用する。

#### 2.2.3 UR Fall Detection Dataset (URFD)

**概要:** ポーランドRzeszow大学が公開した転倒検出研究用データセット。加速度センサーデータとRGB-D動画の両方を含む。

- **URL:** http://fenix.univ.rzeszow.pl/~mkepski/ds/uf.html
- **内容:** 転倒30シーケンス + 日常動作 (ADL) 40シーケンス
- **データ形式:** 動画フレーム (PNG) + 加速度データ + デプスマップ
- **ライセンス:** 研究用 (引用必要)

**利点:**
- 学術的に広く利用されている転倒検出ベンチマーク
- 転倒と日常動作の両方のフレームを取得可能
- 多様な転倒パターン (前方、後方、側方等) を含む

**制限事項:**
- 動画フレームからの切り出しとバウンディングボックスアノテーションの付与が必要
- データ量が比較的少ない (70シーケンス)
- 背景が限定的 (実験室環境)

**推奨使用方法:** 動画フレームを一定間隔でサンプリングし (例: 5フレームごと)、人物検出の自動アノテーション (事前学習済みモデルによる半自動) + 手動修正でバウンディングボックスを付与する。転倒シーンの補強データとして300-500枚を確保する。

#### 2.2.4 Le2i Fall Detection Dataset

**概要:** フランスLe2i研究所が公開した転倒検出用動画データセット。家庭環境に近い室内シーンを収録。

- **URL:** http://le2i.cnrs.fr/Fall-detection-Dataset (またはミラーサイト)
- **内容:** Coffee room, Home, Office, Lecture room の4環境で撮影された約220動画
- **データ形式:** 動画 (AVI) + フレームレベルのfall/not-fallラベル
- **ライセンス:** 研究用

**利点:**
- 家庭環境に近い室内シーンを含む (本プロジェクトの使用環境に合致)
- 4種類の異なる環境で撮影されており、背景の多様性がある
- 転倒と紛らわしい動作 (confounding actions) のデータも含む

**制限事項:**
- バウンディングボックスアノテーションの付与が必要
- 入手に申請が必要な場合がある

**推奨使用方法:** 動画フレームからサンプリングし、半自動アノテーションでバウンディングボックスを付与する。特に室内環境の転倒シーンとして500-1,000枚を確保する。

#### 2.2.5 DiverseFall10500

**概要:** Mendeley Dataで公開されている大規模転倒検出画像データセット。多様な転倒パターンを含む。

- **URL:** https://data.mendeley.com/ で "DiverseFall10500" を検索
- **画像数:** 10,500枚
- **アノテーション:** YOLO TXT形式
- **クラス:** fall / not-fall
- **ライセンス:** CC BY 4.0

**利点:**
- 大規模で多様な転倒パターンを含む
- YOLO TXT形式で提供されており変換作業が最小限
- 学術論文で引用されており品質が一定以上保証されている

**制限事項:**
- fall/not-fall の2クラスを "person" 1クラスに統合する変換が必要
- Mendeley Dataからのダウンロードに時間がかかる場合がある

**推奨使用方法:** 全データまたはサブセットをダウンロードし、クラスを "person" に統合して使用する。転倒シーンの大量データとして非常に有用。

#### 2.2.6 GMDCSA-24 Dataset

**概要:** Mendeley Dataで公開されている2024年の新しい転倒検出データセット。44,000枚以上の画像を含む大規模データセット。

- **URL:** https://data.mendeley.com/ で "GMDCSA-24" を検索
- **画像数:** 44,000枚以上
- **アノテーション:** YOLO TXT形式
- **クラス:** fall / not-fall
- **ライセンス:** CC BY 4.0

**利点:**
- 非常に大規模なデータセット
- 2024年公開の新しいデータセットで多様なシーンを含む
- YOLO形式で直接利用可能

**制限事項:**
- データサイズが大きくダウンロードに時間がかかる
- 全量使用は過剰なため、サブセット選定が必要

**推奨使用方法:** サブセットを抽出して使用する。特に転倒シーンのバリエーション補強に有用。

#### 2.2.7 Wake Vision Dataset

**概要:** TensorFlow/Google が公開した TinyML 向け人物検出データセット。Visual Wake Words (VWW) タスクのために設計されている。

- **URL:** https://blog.tensorflow.org/2024/12/introducing-wake-vision-new-dataset-for-person-detection-in-tinyml.html
- **画像数:** 約112,000枚
- **タスク:** 画像分類 (person / no-person)
- **ライセンス:** CC BY 4.0

**利点:**
- TinyML向けに設計されており、低解像度画像での人物検出に最適化
- 大規模で多様な環境を含む
- models_tested.md に "vww4_128_128" が記載されておりRUHMI実績あり

**制限事項:**
- 画像分類用でバウンディングボックスアノテーションがない
- 物体検出の学習には直接使用できず、アノテーション付与が必要
- 転倒シーンはほぼ含まれない

**推奨使用方法:** 直接的な物体検出学習には使用しない。参考データセットとして位置づけ、将来的に分類モデルのベースライン比較に利用可能。

### 2.3 推奨データセット構成

以下の組み合わせでデータセットを構成することを推奨する。

| 優先度 | データセット | 抽出枚数 | 役割 | 変換作業 |
|---|---|---|---|---|
| **必須** | COCO 2017 (person) | 5,000-10,000枚 | 通常姿勢 (立位/歩行/座位) の学習ベース | COCO JSON -> YOLO TXT変換 |
| **必須** | Roboflow Fall Detection | 1,000-2,800枚 | 転倒シーンの学習データ | fall/not-fall -> person統合 |
| **推奨** | DiverseFall10500 | 2,000-5,000枚 | 転倒シーンのバリエーション補強 | fall/not-fall -> person統合 |
| 推奨 | Le2i Fall Detection | 500-1,000枚 | 室内環境での転倒シーン補強 | フレーム抽出 + BBox付与 |
| 推奨 | UR Fall Detection | 300-500枚 | 転倒パターンの多様性確保 | フレーム抽出 + BBox付与 |
| 任意 | GMDCSA-24 | 1,000-3,000枚 | 追加の転倒シーン | fall/not-fall -> person統合 |

**合計目標: 約9,000-22,000枚** (最小構成: COCO + Roboflow で約6,000-13,000枚)

注意: 上記の枚数は推奨値であり、学習結果の精度に応じて調整する。初回の学習では最小構成 (COCO 5,000枚 + Roboflow 1,000枚 = 6,000枚) から開始し、精度が不十分な場合にデータを追加する段階的アプローチを推奨する。

---

## 3. アノテーション形式の統一

### 3.1 YOLO Darknet TXT形式の仕様

YOLO-Fastest v1.1はDarknetフレームワークで学習するため、アノテーションはYOLO Darknet TXT形式に統一する。

#### ファイル構成

各画像ファイルに対応する同名の `.txt` ファイルを配置する:

```
dataset/
  images/
    img_0001.jpg
    img_0002.jpg
    ...
  labels/
    img_0001.txt
    img_0002.txt
    ...
```

#### アノテーション形式

各 `.txt` ファイルの1行が1つのバウンディングボックスに対応する:

```
<class_id> <x_center> <y_center> <width> <height>
```

| フィールド | 説明 | 値の範囲 |
|---|---|---|
| class_id | クラスID (0始まり) | 本プロジェクトでは常に 0 (person) |
| x_center | バウンディングボックス中心のX座標 (画像幅で正規化) | 0.0 - 1.0 |
| y_center | バウンディングボックス中心のY座標 (画像高さで正規化) | 0.0 - 1.0 |
| width | バウンディングボックスの幅 (画像幅で正規化) | 0.0 - 1.0 |
| height | バウンディングボックスの高さ (画像高さで正規化) | 0.0 - 1.0 |

#### 記載例

1枚の画像に2人の人物が写っている場合:

```
0 0.4531 0.6250 0.2344 0.7500
0 0.7812 0.5000 0.1562 0.8000
```

### 3.2 COCO JSON から YOLO TXT への変換スクリプト

COCO 2017データセットの "person" クラスを抽出し、YOLO Darknet TXT形式に変換する手順を示す。

#### 変換スクリプト (coco_to_yolo_person.py)

```python
import json
import os
from pathlib import Path

# COCO person class ID = 1
COCO_PERSON_CLASS_ID = 1
# YOLO class ID = 0 (person)
YOLO_PERSON_CLASS_ID = 0

def coco_to_yolo(coco_json_path, output_dir, image_dir=None):
    with open(coco_json_path) as f:
        coco = json.load(f)

    # Build image ID -> image info mapping
    images = {img["id"]: img for img in coco["images"]}

    os.makedirs(output_dir, exist_ok=True)

    # Filter person annotations
    for ann in coco["annotations"]:
        if ann["category_id"] != COCO_PERSON_CLASS_ID:
            continue
        if ann.get("iscrowd", 0) == 1:
            continue

        img_info = images[ann["image_id"]]
        img_w = img_info["width"]
        img_h = img_info["height"]
        file_name = img_info["file_name"]

        # COCO bbox: [x_min, y_min, width, height]
        x_min, y_min, bbox_w, bbox_h = ann["bbox"]

        # Convert to YOLO format: [x_center, y_center, width, height] (normalized)
        x_center = (x_min + bbox_w / 2) / img_w
        y_center = (y_min + bbox_h / 2) / img_h
        w_norm = bbox_w / img_w
        h_norm = bbox_h / img_h

        # Clamp values to [0, 1]
        x_center = max(0.0, min(1.0, x_center))
        y_center = max(0.0, min(1.0, y_center))
        w_norm = max(0.0, min(1.0, w_norm))
        h_norm = max(0.0, min(1.0, h_norm))

        # Write to YOLO TXT file
        txt_name = Path(file_name).stem + ".txt"
        txt_path = os.path.join(output_dir, txt_name)
        with open(txt_path, "a") as tf:
            tf.write(
                f"{YOLO_PERSON_CLASS_ID} {x_center:.6f} {y_center:.6f} {w_norm:.6f} {h_norm:.6f}\n"
            )

    print(f"Conversion complete. Output: {output_dir}")

# Usage:
# coco_to_yolo("instances_train2017.json", "labels/train")
```

#### 変換手順

1. COCO 2017のアノテーションファイルをダウンロードする
   - `instances_train2017.json` (train用)
   - `instances_val2017.json` (val用)
2. 上記スクリプトを実行し、personクラスのアノテーションのみをYOLO TXT形式で出力する
3. 対応する画像ファイル (train2017/, val2017/) と合わせてデータセットディレクトリを構成する

### 3.3 Roboflow / DiverseFall のクラス統合

Roboflow Fall DetectionやDiverseFall10500等のデータセットは "fall" (class_id=0) と "not-fall" (class_id=1) の2クラスでアノテーションされている場合が多い。これを "person" (class_id=0) の1クラスに統合する。

#### 統合スクリプト (unify_classes.py)

```python
import os
import glob

def unify_to_person(label_dir):
    txt_files = glob.glob(os.path.join(label_dir, "*.txt"))
    for txt_path in txt_files:
        with open(txt_path) as f:
            lines_in = f.readlines()
        lines_out = []
        for line in lines_in:
            parts = line.strip().split()
            if len(parts) >= 5:
                # Replace class_id with 0 (person)
                parts[0] = "0"
                lines_out.append(" ".join(parts))
        with open(txt_path, "w") as f:
            f.write("\n".join(lines_out) + "\n")
    print(f"Unified {len(txt_files)} files to person class.")

# Usage:
# unify_to_person("labels/fall_detection/")
```

### 3.4 動画フレームからのバウンディングボックス付与 (半自動)

UR Fall DetectionやLe2i等の動画データセットでは、フレームを切り出した後にバウンディングボックスアノテーションを付与する必要がある。以下の半自動アプローチを推奨する。

#### 手順

1. **動画フレーム抽出:** ffmpegで一定間隔のフレームを抽出する
```bash
# 5フレームごとに抽出
ffmpeg -i input_video.avi -vf "select=not(mod(n\,5))" -vsync vfr output_%04d.jpg
```

2. **事前学習済みモデルによる自動アノテーション:** YOLOv5やYOLOv8等のCOCO事前学習済みモデルでpersonクラスのバウンディングボックスを自動生成する
```python
from ultralytics import YOLO
model = YOLO("yolov8n.pt")  # COCO pre-trained
results = model.predict(source="frames/", save_txt=True, classes=[0])  # class 0 = person
```

3. **手動修正:** LabelImgやCVAT等のアノテーションツールで自動生成結果を確認・修正する
   - 推奨ツール: [CVAT](https://www.cvat.ai/) (Webベース), [LabelImg](https://github.com/HumanSignal/labelImg) (デスクトップ)
   - 特に転倒シーンでは自動アノテーションの精度が低い場合があるため、入念な確認が必要

---

## 4. データ拡張戦略

### 4.1 データ拡張の目的

- 学習データの実効的なバリエーションを増やし、過学習を抑制する
- 実運用環境 (家庭内の多様な照明条件、カメラ角度) への汎化性能を向上させる
- Grayscale 192x192入力という制約条件下での頑健性を確保する

### 4.2 Darknet学習時の組み込みデータ拡張

YOLO-Fastest v1.1はDarknetフレームワークで学習する。Darknetには以下の組み込みデータ拡張機能がある。

| パラメータ | 説明 | 推奨値 | 備考 |
|---|---|---|---|
| mosaic=1 | 4枚の画像をモザイク状に合成 | 1 (有効) | 小さい物体の検出精度向上に効果的 |
| angle=0 | ランダム回転角度 | 0-15 | 大きな回転はBBox精度低下のリスク |
| saturation=1.5 | 彩度の変動幅 | 1.5 | Grayscale入力では効果なし |
| exposure=1.5 | 露出 (明るさ) の変動幅 | 1.5 | 室内の照明変動をシミュレート |
| hue=0.1 | 色相の変動幅 | 0.1 | Grayscale入力では効果なし |
| jitter=0.3 | ランダムクロップの割合 | 0.3 | 人物の部分的な遮蔽をシミュレート |
| flip=1 | 左右反転 | 1 (有効) | 転倒方向の左右対称性を学習 |

**Grayscale入力での注意点:** saturation と hue のパラメータはGrayscale入力では効果がないため、exposure と幾何学的変換 (jitter, flip, mosaic) に重点を置く。

### 4.3 オフラインデータ拡張 (Albumentations)

Darknetの組み込み拡張に加え、事前にオフラインでデータ拡張を行い、学習データのバリエーションを増やすことを推奨する。

#### 推奨する拡張パイプライン

```python
import albumentations as A

transform = A.Compose([
    A.HorizontalFlip(p=0.5),
    A.Rotate(limit=15, p=0.3, border_mode=0),
    A.RandomBrightnessContrast(brightness_limit=0.3, contrast_limit=0.3, p=0.5),
    A.GaussNoise(var_limit=(10, 50), p=0.3),
    A.Blur(blur_limit=3, p=0.2),
    A.RandomScale(scale_limit=0.2, p=0.3),
], bbox_params=A.BboxParams(format="yolo", min_visibility=0.3))
```

#### 各拡張手法の説明

| 拡張手法 | パラメータ | 目的 | 転倒検出への効果 |
|---|---|---|---|
| HorizontalFlip | p=0.5 | 左右反転 | 転倒方向の対称性学習 |
| Rotate | limit=15度, p=0.3 | 回転 | カメラ設置角度のばらつき対応 |
| RandomBrightnessContrast | brightness=0.3, contrast=0.3 | 明るさ/コントラスト変動 | 室内照明条件のばらつき対応 |
| GaussNoise | var=10-50, p=0.3 | ガウスノイズ付加 | カメラセンサーノイズへの耐性 |
| Blur | blur_limit=3, p=0.2 | ぼかし | ピンぼけ画像への耐性 |
| RandomScale | scale=0.2, p=0.3 | スケール変動 | 人物サイズのばらつき対応 |

**注意:** Albumentationsの bbox_params で format="yolo" を指定することで、バウンディングボックスも画像と同期して変換される。min_visibility=0.3 は、拡張後にバウンディングボックスの30%以上が画像内に残る場合のみ保持する設定である。

### 4.4 Grayscale変換の考慮

本プロジェクトのAIモデルは192x192x1 Grayscale入力である。学習データはRGBカラーで収集されるため、学習時にGrayscale変換を行う必要がある。

#### 変換方式

EK-RA8P1の実機では camera_utils.c の image_rgb565_to_int8() 関数で以下の変換が行われる:

```
gray = (int8_t)((0.299 * R + 0.587 * G + 0.114 * B) - 128)
```

学習データの前処理でも同じITU-R BT.601方式のGrayscale変換を使用し、学習時と推論時の入力分布を一致させることが重要である。

#### Darknet学習時のGrayscale設定

YOLO-Fastest v1.1のDarknet設定ファイル (.cfg) で channels=1 を指定する:

```ini
[net]
batch=64
subdivisions=16
width=192
height=192
channels=1  # Grayscale
```

---

## 5. データ分割戦略

### 5.1 分割比率

| セット | 比率 | 用途 |
|---|---|---|
| Train | 70% | モデルの学習 |
| Validation | 15% | 学習中の精度監視・ハイパーパラメータ調整 |
| Test | 15% | 最終的な精度評価 (学習に使用しない) |

### 5.2 分割時の注意事項

#### データリーケージの防止

動画フレームから抽出したデータセット (UR Fall, Le2i等) では、同一動画の異なるフレームがtrain/val/testに分散するとデータリーケージが発生する。これを防ぐため、**動画単位で分割する** ことが必須である。

```
# 正しい分割: 動画単位
Train:  video_001, video_002, ..., video_049  (70%の動画)
Val:    video_050, ..., video_060              (15%の動画)
Test:   video_061, ..., video_070              (15%の動画)

# 誤った分割: フレーム単位 (データリーケージが発生)
Train:  video_001_frame001, video_001_frame003, ...  # 同一動画が分散
Val:    video_001_frame002, ...                       # リーク!
```

#### 姿勢の偏りの防止

各セットに以下の姿勢が均等に含まれるようにする:

- 立位 (standing)
- 歩行 (walking)
- 座位 (sitting)
- 転倒 (fallen - 横長バウンディングボックス)

特にTestセットには転倒シーンを十分に含める (全転倒画像の15%以上)。

### 5.3 Darknet学習用ファイル構成

Darknetでの学習には以下のファイル構成が必要である:

```
dataset/
  obj.data          # データセット設定ファイル
  obj.names         # クラス名定義
  train.txt         # 学習画像パスのリスト
  valid.txt         # 検証画像パスのリスト
  test.txt          # テスト画像パスのリスト
  images/
    train/
      img_0001.jpg
      ...
    val/
      img_0001.jpg
      ...
    test/
      img_0001.jpg
      ...
  labels/
    train/
      img_0001.txt
      ...
    val/
      img_0001.txt
      ...
    test/
      img_0001.txt
      ...
```

#### obj.data

```
classes = 1
train = dataset/train.txt
valid = dataset/valid.txt
names = dataset/obj.names
backup = backup/
```

#### obj.names

```
person
```

---

## 6. データセット品質保証

### 6.1 品質チェック項目

#### 6.1.1 視覚的チェック

- ランダムに50-100枚を抽出し、バウンディングボックスを画像上に描画して目視確認する
- 以下の問題がないか確認する:
  - バウンディングボックスが人物を正しく囲んでいるか
  - 極端に小さい/大きいバウンディングボックスがないか
  - アノテーション漏れ (人物が写っているがBBoxがない) がないか
  - 誤アノテーション (人物でないものにBBoxが付いている) がないか

#### 6.1.2 統計的チェック

- バウンディングボックスのサイズ分布を確認する
  - 極端に小さいBBox (幅または高さが画像サイズの5%未満) を除外またはフラグ付け
  - 極端に大きいBBox (幅または高さが画像サイズの95%超) を確認
- 1画像あたりのBBox数の分布を確認する
- クラスバランスの確認 (本プロジェクトでは1クラスのため該当しないが、将来の2クラス化に備えて)

#### 6.1.3 アスペクト比分布の確認

アプローチC (アスペクト比判定) の有効性を事前評価するため、学習データのバウンディングボックスのアスペクト比分布を分析する。

```python
import os
import matplotlib.pyplot as plt

aspect_ratios = []
for txt_file in os.listdir("labels/"):
    if not txt_file.endswith(".txt"):
        continue
    with open(os.path.join("labels/", txt_file)) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 5:
                w, h = float(parts[3]), float(parts[4])
                if h > 0:
                    aspect_ratios.append(w / h)

plt.hist(aspect_ratios, bins=50, range=(0, 3))
plt.xlabel("Aspect Ratio (w/h)")
plt.ylabel("Count")
plt.axvline(x=1.0, color="r", linestyle="--", label="w=h (threshold)")
plt.legend()
plt.title("BBox Aspect Ratio Distribution")
plt.savefig("aspect_ratio_distribution.png")
plt.show()
```

**期待される結果:**
- 立位/歩行: アスペクト比 < 1.0 (縦長)
- 座位: アスペクト比 0.7-1.2 程度
- 転倒: アスペクト比 > 1.0 (横長)

この分布から、転倒判定の閾値 (w/h > 1.0) の妥当性を事前に検証できる。

### 6.2 品質チェックリスト

| # | チェック項目 | 確認方法 | 判定基準 |
|---|---|---|---|
| 1 | アノテーション形式 | 全TXTファイルのパース | YOLO形式 (5値) に準拠 |
| 2 | 座標値の範囲 | x_center, y_center, w, h が 0.0-1.0 の範囲内 | 範囲外の値が0件 |
| 3 | 画像-ラベル対応 | 画像ファイルとTXTファイルの1:1対応 | 不整合が0件 |
| 4 | 空ラベルファイル | 内容が空のTXTファイルの数 | 背景画像として意図的な場合のみ許容 |
| 5 | BBoxサイズ | 幅または高さが0.01未満のBBox | 除外または確認 |
| 6 | 視覚的整合性 | ランダムサンプル50枚の目視確認 | 明らかな誤りが0件 |
| 7 | アスペクト比分布 | ヒストグラム分析 | 立位/転倒の分離が確認できる |
| 8 | データリーケージ | 動画由来データの分割確認 | 同一動画がtrain/val/testに分散していない |

---

## 7. 転倒シーンのバリエーション

Issue #19の受け入れ条件として、3種類以上の転倒シーンバリエーションの記載が求められている。以下に、データセットで網羅すべき転倒パターンと紛らわしい非転倒パターンを列挙する。

### 7.1 転倒パターン (Fall Patterns)

| # | パターン | 説明 | BBoxアスペクト比 (想定) | データソース |
|---|---|---|---|---|
| 1 | **前方転倒 (Forward Fall)** | 前方に倒れる。うつ伏せ状態になる | w/h > 1.5 | UR Fall, Le2i, Roboflow |
| 2 | **後方転倒 (Backward Fall)** | 後方に倒れる。仰向け状態になる | w/h > 1.5 | UR Fall, Le2i, Roboflow |
| 3 | **側方転倒 (Lateral Fall)** | 左右に倒れる。横向き状態になる | w/h > 1.2 | UR Fall, Le2i, Roboflow |
| 4 | **膝から崩れる転倒 (Collapse)** | 膝から崩れ落ちる。途中経過で座位に近い姿勢を経由 | w/h: 0.8-1.5 (遷移中) | UR Fall, DiverseFall |
| 5 | **椅子からの転落 (Fall from Chair)** | 椅子に座った状態から転落する | w/h > 1.2 | Le2i, Roboflow |
| 6 | **つまずき転倒 (Trip and Fall)** | 歩行中につまずいて転倒 | w/h: 立位 -> 横長に急変 | DiverseFall, GMDCSA-24 |
| 7 | **ベッドからの転落 (Fall from Bed)** | ベッドから落下する | w/h > 1.0 | 独自撮影推奨 |

### 7.2 紛らわしい非転倒パターン (Confounding Patterns)

アプローチC (アスペクト比判定) では以下のパターンが誤検出のリスクとなるため、学習データに十分含める必要がある。

| # | パターン | 説明 | BBoxアスペクト比 (想定) | 誤検出リスク |
|---|---|---|---|---|
| 1 | **座位 (Sitting)** | 椅子やソファに座っている | w/h: 0.7-1.2 | 中 (横長に近い場合) |
| 2 | **しゃがみ (Crouching)** | しゃがんだ姿勢 | w/h: 0.8-1.5 | 高 (横長になりやすい) |
| 3 | **寝転び (Lying on purpose)** | 意図的に横になっている (ストレッチ等) | w/h > 1.5 | 高 (転倒と区別困難) |
| 4 | **屈み (Bending)** | 物を拾うために屈んでいる | w/h: 0.8-1.3 | 中 |

**注意:** パターン3 (意図的な寝転び) はアスペクト比判定のみでは転倒と区別できない。フレーム間の急激な変化 (立位から横長への急変) を検出する時系列分析で補完する必要がある。これはF-003-1で記載した段階的精度改善パス (Phase 2) の対象である。

---

## 8. 実行手順のまとめ

以下の手順でデータセットを準備する。

### Step 1: データセットのダウンロード

| # | データセット | ダウンロード方法 | 推奨枚数 |
|---|---|---|---|
| 1 | COCO 2017 (train2017 + annotations) | https://cocodataset.org/ からダウンロード | person画像5,000-10,000枚抽出 |
| 2 | Roboflow Fall Detection | Roboflow UniverseからYOLO形式でエクスポート | 全量 (1,000-2,800枚) |
| 3 | DiverseFall10500 | Mendeley Dataからダウンロード | 2,000-5,000枚サブセット |
| 4 | UR Fall Detection (任意) | 公式サイトからダウンロード | フレーム抽出300-500枚 |
| 5 | Le2i Fall Detection (任意) | 公式サイトから申請・ダウンロード | フレーム抽出500-1,000枚 |

### Step 2: アノテーション形式の統一

1. COCO JSON -> YOLO TXT変換 (Section 3.2のスクリプトを使用)
2. Roboflow/DiverseFall のクラス統合: fall/not-fall -> person (Section 3.3のスクリプトを使用)
3. 動画データセットのフレーム抽出 + 半自動アノテーション (Section 3.4の手順)

### Step 3: データセットの統合と分割

1. 全データセットを統一ディレクトリに配置
2. 動画由来データは動画単位で分割 (Section 5.2)
3. Train/Val/Test = 70/15/15 で分割
4. train.txt, valid.txt, test.txt を生成

### Step 4: 品質チェック

1. アノテーション形式の検証 (Section 6.2のチェックリスト)
2. ランダムサンプルの目視確認
3. アスペクト比分布の分析 (Section 6.1.3)
4. BBoxサイズ分布の確認

### Step 5: Darknet学習用ファイル生成

1. obj.data, obj.names の作成 (Section 5.3)
2. 画像パスリスト (train.txt, valid.txt) の最終確認

### Step 6: オフラインデータ拡張 (任意)

1. Albumentationsパイプラインの実行 (Section 4.3)
2. 拡張後のデータセットサイズ確認

---

## 9. 追加調査が必要な項目

| # | 項目 | 理由 | 優先度 |
|---|---|---|---|
| 1 | COCO 2017 personクラスの実際の画像枚数確認 | 本レポートでは約64,000枚と記載したが、実際にダウンロードして確認が必要 | 高 |
| 2 | Roboflow Fall Detectionの具体的プロジェクト選定 | 複数のバリエーションがあるため、最適なものを実際に確認して選定する | 高 |
| 3 | DiverseFall10500のデータ品質確認 | ダウンロード後にアノテーション品質を目視確認する | 高 |
| 4 | Grayscale変換後の人物検出精度への影響 | 色情報なしでの検出精度を小規模実験で確認する必要がある | 中 |
| 5 | アスペクト比判定の閾値の予備検証 | 学習データのBBoxアスペクト比分布から適切な閾値を事前検討する | 中 |
| 6 | Le2i/UR Fall Detectionの入手手続き | 研究用データセットの利用申請が必要な場合の手続き確認 | 中 |
| 7 | 独自撮影データの必要性判断 | 公開データセットで十分な精度が得られない場合、EK-RA8P1のカメラで独自撮影データを追加する | 低 (精度評価後) |

---

## 10. 参考資料

### 10.1 データセット

- [COCO Dataset](https://cocodataset.org/) - Microsoft COCO: Common Objects in Context
- [Roboflow Universe](https://universe.roboflow.com/) - Fall Detection関連プロジェクト
- [UR Fall Detection Dataset](http://fenix.univ.rzeszow.pl/~mkepski/ds/uf.html) - University of Rzeszow
- [Le2i Fall Detection Dataset](http://le2i.cnrs.fr/Fall-detection-Dataset) - Laboratoire Le2i
- [DiverseFall10500 (Mendeley Data)](https://data.mendeley.com/) - 大規模転倒検出画像データセット
- [GMDCSA-24 (Mendeley Data)](https://data.mendeley.com/) - 2024年公開転倒検出データセット
- [Wake Vision Dataset](https://blog.tensorflow.org/2024/12/introducing-wake-vision-new-dataset-for-person-detection-in-tinyml.html) - TinyML向け人物検出データセット

### 10.2 ツール・ライブラリ

- [pycocotools](https://github.com/cocodataset/cocoapi) - COCO APIライブラリ
- [Albumentations](https://albumentations.ai/) - データ拡張ライブラリ
- [CVAT](https://www.cvat.ai/) - Computer Vision Annotation Tool
- [LabelImg](https://github.com/HumanSignal/labelImg) - 画像アノテーションツール
- [Ultralytics YOLOv8](https://github.com/ultralytics/ultralytics) - 半自動アノテーション用

### 10.3 関連ドキュメント

- [F-003-1: 転倒検出AIモデル調査・選定レポート](f003_01_model_selection_report.md) - モデル選定結果
- [RUHMIフレームワーク解析レポート](ruhmi_framework_mcu_face_detection_analysis.md) - 顔認識サンプルの詳細解析
- [RUHMI検証済みモデル一覧](../../reference_projects/ruhmi-framework-mcu/docs/models_tested.md) - EK-RA8P1で動作確認済みモデル

### 10.4 転倒検出研究

- [Human Fall Detection using Normalized Shape Aspect Ratio](https://www.researchgate.net/publication/328746754) - アスペクト比による転倒検出
- [Fall Detection System With AI-Based Edge Computing](https://www.researchgate.net/publication/357596359) - エッジAI転倒検出
- [Nota-NetsPresso YOLO-Fastest for ARM U55/M85](https://github.com/Nota-NetsPresso/ModelZoo-YOLOFastest-for-ARM-U55-M85) - 学習チュートリアル

---

## 改訂履歴

| 日付 | バージョン | 変更内容 | 作成者 |
|------|-----------|---------|--------|
| 2026-03-02 | 1.0 | 初版作成 | Claude Code |
