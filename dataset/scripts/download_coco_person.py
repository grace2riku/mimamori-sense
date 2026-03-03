"""
COCO 2017 データセットから person クラスの画像を抽出・ダウンロードするスクリプト

使い方:
  python download_coco_person.py --max-images 5000
"""

import argparse
import json
import os
import random
import shutil
import sys
import zipfile
from pathlib import Path
from urllib.request import urlretrieve

import requests
from tqdm import tqdm

# ---- 定数 ----
COCO_ANNOTATIONS_URL = "http://images.cocodataset.org/annotations/annotations_trainval2017.zip"
COCO_TRAIN_IMAGES_URL = "http://images.cocodataset.org/zips/train2017.zip"
COCO_VAL_IMAGES_URL = "http://images.cocodataset.org/zips/val2017.zip"
COCO_PERSON_CATEGORY_ID = 1
YOLO_PERSON_CLASS_ID = 0

SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent
DOWNLOAD_DIR = DATASET_ROOT / "downloads" / "coco"
OUTPUT_DIR = DATASET_ROOT / "coco_person"


class TqdmUpTo(tqdm):
    """tqdm を urlretrieve の reporthook に使うラッパー"""
    def update_to(self, blocks=1, block_size=1, total_size=None):
        if total_size is not None:
            self.total = total_size
        self.update(blocks * block_size - self.n)


def download_file(url: str, dest: Path):
    """URL からファイルをダウンロード（プログレスバー付き）"""
    if dest.exists():
        print(f"  Already exists: {dest}")
        return
    dest.parent.mkdir(parents=True, exist_ok=True)
    print(f"  Downloading: {url}")
    with TqdmUpTo(unit="B", unit_scale=True, miniters=1, desc=dest.name) as t:
        urlretrieve(url, str(dest), reporthook=t.update_to)
    print(f"  Saved: {dest}")


def extract_zip(zip_path: Path, extract_to: Path):
    """ZIP を展開"""
    print(f"  Extracting: {zip_path}")
    with zipfile.ZipFile(zip_path, "r") as zf:
        zf.extractall(extract_to)
    print(f"  Extracted to: {extract_to}")


def coco_bbox_to_yolo(bbox, img_w, img_h):
    """COCO bbox [x_min, y_min, w, h] -> YOLO [x_center, y_center, w, h] (正規化)"""
    x_min, y_min, bw, bh = bbox
    x_center = (x_min + bw / 2) / img_w
    y_center = (y_min + bh / 2) / img_h
    w_norm = bw / img_w
    h_norm = bh / img_h
    # clamp
    x_center = max(0.0, min(1.0, x_center))
    y_center = max(0.0, min(1.0, y_center))
    w_norm = max(0.0, min(1.0, w_norm))
    h_norm = max(0.0, min(1.0, h_norm))
    return x_center, y_center, w_norm, h_norm


def process_coco(max_images: int, seed: int):
    """メイン処理"""
    # 1. アノテーションダウンロード
    print("\n[Step 1/4] Downloading COCO 2017 annotations...")
    ann_zip = DOWNLOAD_DIR / "annotations_trainval2017.zip"
    download_file(COCO_ANNOTATIONS_URL, ann_zip)

    ann_json_path = DOWNLOAD_DIR / "annotations" / "instances_train2017.json"
    if not ann_json_path.exists():
        extract_zip(ann_zip, DOWNLOAD_DIR)

    # 2. person クラスの画像 ID を抽出
    print("\n[Step 2/4] Extracting person-class image IDs...")
    with open(ann_json_path) as f:
        coco = json.load(f)

    images_map = {img["id"]: img for img in coco["images"]}

    # person アノテーションを画像 ID ごとに集める
    person_anns = {}  # image_id -> list of annotations
    for ann in coco["annotations"]:
        if ann["category_id"] != COCO_PERSON_CATEGORY_ID:
            continue
        if ann.get("iscrowd", 0) == 1:
            continue
        # 極端に小さい bbox はスキップ
        _, _, bw, bh = ann["bbox"]
        if bw < 10 or bh < 10:
            continue
        img_id = ann["image_id"]
        if img_id not in person_anns:
            person_anns[img_id] = []
        person_anns[img_id].append(ann)

    all_person_image_ids = list(person_anns.keys())
    print(f"  Found {len(all_person_image_ids)} images with person annotations")

    # ランダムサンプリング
    random.seed(seed)
    if max_images < len(all_person_image_ids):
        selected_ids = set(random.sample(all_person_image_ids, max_images))
    else:
        selected_ids = set(all_person_image_ids)
    print(f"  Selected {len(selected_ids)} images (max_images={max_images}, seed={seed})")

    # 3. 画像をダウンロード（COCO の個別画像 URL を使用）
    print("\n[Step 3/4] Downloading selected images...")
    images_dir = OUTPUT_DIR / "images"
    labels_dir = OUTPUT_DIR / "labels"
    images_dir.mkdir(parents=True, exist_ok=True)
    labels_dir.mkdir(parents=True, exist_ok=True)

    downloaded = 0
    skipped = 0
    errors = 0

    for img_id in tqdm(sorted(selected_ids), desc="Downloading images"):
        img_info = images_map[img_id]
        file_name = img_info["file_name"]
        coco_url = img_info.get("coco_url", "")

        dest_img = images_dir / file_name
        if dest_img.exists():
            skipped += 1
        else:
            try:
                r = requests.get(coco_url, timeout=30)
                r.raise_for_status()
                with open(dest_img, "wb") as f:
                    f.write(r.content)
                downloaded += 1
            except Exception as e:
                print(f"\n  Error downloading {file_name}: {e}")
                errors += 1
                continue

        # YOLO ラベル生成
        img_w = img_info["width"]
        img_h = img_info["height"]
        label_path = labels_dir / (Path(file_name).stem + ".txt")
        with open(label_path, "w") as lf:
            for ann in person_anns[img_id]:
                xc, yc, wn, hn = coco_bbox_to_yolo(ann["bbox"], img_w, img_h)
                lf.write(f"{YOLO_PERSON_CLASS_ID} {xc:.6f} {yc:.6f} {wn:.6f} {hn:.6f}\n")

    print(f"\n  Downloaded: {downloaded}, Skipped (exist): {skipped}, Errors: {errors}")

    # 4. サマリ出力
    print("\n[Step 4/4] Summary")
    n_images = len(list(images_dir.glob("*.jpg")))
    n_labels = len(list(labels_dir.glob("*.txt")))
    print(f"  Images: {n_images}")
    print(f"  Labels: {n_labels}")
    print(f"  Output: {OUTPUT_DIR}")


def main():
    parser = argparse.ArgumentParser(description="Download COCO 2017 person-class images")
    parser.add_argument("--max-images", type=int, default=5000,
                        help="Maximum number of person images to download (default: 5000)")
    parser.add_argument("--seed", type=int, default=42,
                        help="Random seed for sampling (default: 42)")
    args = parser.parse_args()

    process_coco(args.max_images, args.seed)


if __name__ == "__main__":
    main()
