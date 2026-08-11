"""
オフラインデータ拡張スクリプト (Albumentations)

統合済みデータセットの Train 画像に対してデータ拡張を適用し、
学習データのバリエーションを増やす。

拡張パイプライン:
  - HorizontalFlip (p=0.5): 左右反転 (転倒方向の対称性学習)
  - Rotate (limit=15, p=0.3): 回転 (カメラ設置角度のばらつき対応)
  - RandomBrightnessContrast (p=0.5): 明るさ/コントラスト変動 (室内照明条件)
  - GaussNoise (p=0.3): ガウスノイズ付加 (カメラセンサーノイズ耐性)
  - Blur (blur_limit=3, p=0.2): ぼかし (ピンぼけ耐性)
  - RandomScale (scale=0.2, p=0.3): スケール変動 (人物サイズのばらつき)

使い方:
  python augment_offline.py
  python augment_offline.py --multiplier 2 --seed 42
"""

import argparse
import cv2
import albumentations as A
from pathlib import Path
from tqdm import tqdm

SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent
MERGED_DIR = DATASET_ROOT / "merged"

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


def create_transform() -> A.Compose:
    """レポート Section 4.3 に基づく拡張パイプラインを作成"""
    return A.Compose(
        [
            A.HorizontalFlip(p=0.5),
            A.Rotate(limit=15, p=0.3, border_mode=0),
            A.RandomBrightnessContrast(
                brightness_limit=0.3, contrast_limit=0.3, p=0.5
            ),
            A.GaussNoise(p=0.3),
            A.Blur(blur_limit=3, p=0.2),
            A.RandomScale(scale_limit=0.2, p=0.3),
        ],
        bbox_params=A.BboxParams(
            format="yolo", min_visibility=0.3, label_fields=["class_ids"]
        ),
    )


def read_yolo_labels(label_path: Path) -> tuple[list[list[float]], list[int]]:
    """YOLOラベルファイルを読み込む"""
    bboxes = []
    class_ids = []
    if not label_path.exists():
        return bboxes, class_ids
    with open(label_path) as f:
        for line in f:
            parts = line.strip().split()
            if len(parts) != 5:
                continue
            class_id = int(parts[0])
            bbox = [float(v) for v in parts[1:]]
            # clamp to valid range for albumentations
            bbox = [max(0.0, min(1.0, v)) for v in bbox]
            # width/height must be > 0
            if bbox[2] <= 0 or bbox[3] <= 0:
                continue
            bboxes.append(bbox)
            class_ids.append(class_id)
    return bboxes, class_ids


def write_yolo_labels(
    label_path: Path, bboxes: list[list[float]], class_ids: list[int]
):
    """YOLOラベルファイルを書き出す"""
    with open(label_path, "w") as f:
        for bbox, cls_id in zip(bboxes, class_ids):
            # albumentations は label_fields の値を float 化して返すため、
            # そのまま書くとクラス ID が "0.0" になる。YOLO 仕様は整数であり、
            # darknet は "%d %f %f %f %f" で読むため float 表記だとフィールドが
            # ずれて読まれる (Issue #148、方針書 f003_03l 8 章 R8)。
            f.write(f"{int(cls_id)} {bbox[0]:.6f} {bbox[1]:.6f} "
                    f"{bbox[2]:.6f} {bbox[3]:.6f}\n")


def augment_train(multiplier: int, seed: int):
    """Train画像に対してデータ拡張を実行"""
    train_img_dir = MERGED_DIR / "images" / "train"
    train_lbl_dir = MERGED_DIR / "labels" / "train"

    if not train_img_dir.exists():
        print(f"Error: {train_img_dir} not found. Run merge_and_split.py first.")
        return

    # 元画像の一覧
    image_files = sorted(
        [p for p in train_img_dir.iterdir() if p.suffix.lower() in IMAGE_EXTENSIONS]
    )
    print(f"Original train images: {len(image_files)}")
    print(f"Augmentation multiplier: {multiplier}x")
    print(f"Expected augmented images: {len(image_files) * multiplier}")

    transform = create_transform()
    aug_count = 0
    skip_count = 0

    import random
    random.seed(seed)

    for img_path in tqdm(image_files, desc="Augmenting"):
        # 画像読み込み
        image = cv2.imread(str(img_path))
        if image is None:
            skip_count += 1
            continue
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        # ラベル読み込み
        label_path = train_lbl_dir / (img_path.stem + ".txt")
        bboxes, class_ids = read_yolo_labels(label_path)

        if not bboxes:
            skip_count += 1
            continue

        # multiplier 回の拡張を生成
        for i in range(multiplier):
            try:
                result = transform(
                    image=image, bboxes=bboxes, class_ids=class_ids
                )
            except Exception:
                continue

            aug_image = result["image"]
            aug_bboxes = result["bboxes"]
            aug_class_ids = result["class_ids"]

            # 拡張後にBBoxが残っている場合のみ保存
            if not aug_bboxes:
                continue

            # ファイル名: 元の名前_aug{i}
            aug_stem = f"{img_path.stem}_aug{i}"
            aug_img_path = train_img_dir / f"{aug_stem}{img_path.suffix}"
            aug_lbl_path = train_lbl_dir / f"{aug_stem}.txt"

            # 保存
            aug_bgr = cv2.cvtColor(aug_image, cv2.COLOR_RGB2BGR)
            cv2.imwrite(str(aug_img_path), aug_bgr)
            write_yolo_labels(aug_lbl_path, aug_bboxes, aug_class_ids)
            aug_count += 1

    print(f"\nAugmentation complete:")
    print(f"  Generated: {aug_count} augmented images")
    print(f"  Skipped: {skip_count} (no image/no bbox)")
    print(f"  Total train images: {len(image_files) + aug_count}")

    # train.txt を更新
    print("\nUpdating train.txt...")
    all_train_images = sorted(
        [p for p in train_img_dir.iterdir() if p.suffix.lower() in IMAGE_EXTENSIONS]
    )
    train_txt = MERGED_DIR / "train.txt"
    with open(train_txt, "w") as f:
        for p in all_train_images:
            f.write(str(p) + "\n")
    print(f"  train.txt updated: {len(all_train_images)} entries")


def main():
    parser = argparse.ArgumentParser(description="Offline data augmentation with Albumentations")
    parser.add_argument(
        "--multiplier",
        type=int,
        default=2,
        help="Number of augmented copies per image (default: 2)",
    )
    parser.add_argument(
        "--seed", type=int, default=42, help="Random seed (default: 42)"
    )
    args = parser.parse_args()

    augment_train(args.multiplier, args.seed)


if __name__ == "__main__":
    main()
