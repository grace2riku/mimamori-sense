"""
データセットの統合と Train/Val/Test 分割スクリプト

COCO person と Roboflow Fall Detection を統合し、
Darknet 学習用ディレクトリ構造に分割する。

使い方:
  python merge_and_split.py
  python merge_and_split.py --train-ratio 0.70 --val-ratio 0.15 --seed 42
"""

import argparse
import random
import shutil
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent

# 入力ディレクトリ
COCO_DIR = DATASET_ROOT / "coco_person"
ROBOFLOW_DIR = DATASET_ROOT / "roboflow_fall"

# 出力ディレクトリ
MERGED_DIR = DATASET_ROOT / "merged"

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


def collect_pairs(source_dir: Path, prefix: str) -> list[tuple[str, Path, Path]]:
    """画像とラベルのペアを収集する。

    Returns:
        list of (new_filename_stem, image_path, label_path)
    """
    images_dir = source_dir / "images"
    labels_dir = source_dir / "labels"

    if not images_dir.exists():
        print(f"  Warning: {images_dir} not found, skipping")
        return []

    pairs = []
    for img_path in sorted(images_dir.iterdir()):
        if img_path.suffix.lower() not in IMAGE_EXTENSIONS:
            continue
        label_path = labels_dir / (img_path.stem + ".txt")
        if not label_path.exists():
            continue
        # プレフィックス付きの新ファイル名 (衝突回避)
        new_stem = f"{prefix}_{img_path.stem}"
        pairs.append((new_stem, img_path, label_path))

    return pairs


def split_pairs(
    pairs: list[tuple[str, Path, Path]],
    train_ratio: float,
    val_ratio: float,
    seed: int,
) -> tuple[list, list, list]:
    """ペアを Train/Val/Test に分割する。"""
    random.seed(seed)
    shuffled = pairs.copy()
    random.shuffle(shuffled)

    n = len(shuffled)
    n_train = int(n * train_ratio)
    n_val = int(n * val_ratio)

    train = shuffled[:n_train]
    val = shuffled[n_train : n_train + n_val]
    test = shuffled[n_train + n_val :]

    return train, val, test


def copy_to_split(
    pairs: list[tuple[str, Path, Path]],
    split_name: str,
    merged_dir: Path,
) -> list[str]:
    """ペアを split ディレクトリにコピーし、画像パスリストを返す。"""
    img_out = merged_dir / "images" / split_name
    lbl_out = merged_dir / "labels" / split_name
    img_out.mkdir(parents=True, exist_ok=True)
    lbl_out.mkdir(parents=True, exist_ok=True)

    paths = []
    for new_stem, img_src, lbl_src in pairs:
        img_ext = img_src.suffix
        img_dst = img_out / f"{new_stem}{img_ext}"
        lbl_dst = lbl_out / f"{new_stem}.txt"

        shutil.copy2(img_src, img_dst)
        shutil.copy2(lbl_src, lbl_dst)
        paths.append(str(img_dst))

    return paths


def write_path_list(path_list: list[str], out_file: Path):
    """画像パスリストをファイルに書き出す。"""
    with open(out_file, "w") as f:
        for p in sorted(path_list):
            f.write(p + "\n")


def main():
    parser = argparse.ArgumentParser(
        description="Merge and split datasets for Darknet training"
    )
    parser.add_argument(
        "--train-ratio", type=float, default=0.70, help="Train split ratio (default: 0.70)"
    )
    parser.add_argument(
        "--val-ratio", type=float, default=0.15, help="Validation split ratio (default: 0.15)"
    )
    parser.add_argument(
        "--seed", type=int, default=42, help="Random seed (default: 42)"
    )
    args = parser.parse_args()

    test_ratio = 1.0 - args.train_ratio - args.val_ratio
    print(f"Split ratios: Train={args.train_ratio:.0%}, Val={args.val_ratio:.0%}, Test={test_ratio:.0%}")
    print(f"Random seed: {args.seed}")

    # 1. 各データセットからペアを収集
    print("\n[Step 1/4] Collecting image-label pairs...")
    coco_pairs = collect_pairs(COCO_DIR, "coco")
    print(f"  COCO person: {len(coco_pairs)} pairs")

    roboflow_pairs = collect_pairs(ROBOFLOW_DIR, "rf")
    print(f"  Roboflow fall: {len(roboflow_pairs)} pairs")

    all_pairs = coco_pairs + roboflow_pairs
    print(f"  Total: {len(all_pairs)} pairs")

    if len(all_pairs) == 0:
        print("Error: No image-label pairs found. Run download scripts first.")
        return

    # 2. 出力ディレクトリの準備
    print("\n[Step 2/4] Preparing output directory...")
    if MERGED_DIR.exists():
        shutil.rmtree(MERGED_DIR)
        print(f"  Removed existing: {MERGED_DIR}")
    MERGED_DIR.mkdir(parents=True)

    # 3. 分割とコピー
    print("\n[Step 3/4] Splitting and copying files...")
    train_pairs, val_pairs, test_pairs = split_pairs(
        all_pairs, args.train_ratio, args.val_ratio, args.seed
    )

    train_paths = copy_to_split(train_pairs, "train", MERGED_DIR)
    print(f"  Train: {len(train_paths)} images")

    val_paths = copy_to_split(val_pairs, "val", MERGED_DIR)
    print(f"  Val:   {len(val_paths)} images")

    test_paths = copy_to_split(test_pairs, "test", MERGED_DIR)
    print(f"  Test:  {len(test_paths)} images")

    # 4. Darknet 用ファイル生成
    print("\n[Step 4/4] Generating Darknet config files...")

    # train.txt, valid.txt, test.txt
    write_path_list(train_paths, MERGED_DIR / "train.txt")
    write_path_list(val_paths, MERGED_DIR / "valid.txt")
    write_path_list(test_paths, MERGED_DIR / "test.txt")
    print("  Created: train.txt, valid.txt, test.txt")

    # obj.names
    with open(MERGED_DIR / "obj.names", "w") as f:
        f.write("person\n")
    print("  Created: obj.names")

    # obj.data
    with open(MERGED_DIR / "obj.data", "w") as f:
        f.write(f"classes = 1\n")
        f.write(f"train = {MERGED_DIR / 'train.txt'}\n")
        f.write(f"valid = {MERGED_DIR / 'valid.txt'}\n")
        f.write(f"names = {MERGED_DIR / 'obj.names'}\n")
        f.write(f"backup = backup/\n")
    print("  Created: obj.data")

    # サマリ
    print("\n=== Summary ===")
    print(f"  Total pairs:  {len(all_pairs)}")
    print(f"  Train:        {len(train_paths)} ({len(train_paths)/len(all_pairs):.1%})")
    print(f"  Validation:   {len(val_paths)} ({len(val_paths)/len(all_pairs):.1%})")
    print(f"  Test:         {len(test_paths)} ({len(test_paths)/len(all_pairs):.1%})")
    print(f"  Output:       {MERGED_DIR}")

    # データソース別の内訳
    coco_stems = {p[0] for p in coco_pairs}
    rf_stems = {p[0] for p in roboflow_pairs}
    for split_name, split_pairs_list in [("Train", train_pairs), ("Val", val_pairs), ("Test", test_pairs)]:
        n_coco = sum(1 for p in split_pairs_list if p[0] in coco_stems)
        n_rf = sum(1 for p in split_pairs_list if p[0] in rf_stems)
        print(f"  {split_name:10s} breakdown: COCO={n_coco}, Roboflow={n_rf}")


if __name__ == "__main__":
    main()
