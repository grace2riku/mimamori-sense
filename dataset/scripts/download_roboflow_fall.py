"""
Roboflow Fall Detection データセットをダウンロードするスクリプト

使い方:
  1. Roboflow アカウントを作成 (https://app.roboflow.com/)
  2. API Key を取得 (Settings > API Key)
  3. 実行:
     python download_roboflow_fall.py --api-key YOUR_API_KEY

  または環境変数で指定:
     set ROBOFLOW_API_KEY=YOUR_API_KEY
     python download_roboflow_fall.py
"""

import argparse
import glob
import os
import shutil
from pathlib import Path

from roboflow import Roboflow

SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent
OUTPUT_DIR = DATASET_ROOT / "roboflow_fall"


def unify_classes(label_dir: Path):
    """fall/not-fall の2クラスを person (class_id=0) の1クラスに統合"""
    txt_files = list(label_dir.glob("*.txt"))
    modified = 0
    for txt_path in txt_files:
        if txt_path.name == "classes.txt":
            continue
        with open(txt_path) as f:
            lines_in = f.readlines()
        lines_out = []
        changed = False
        for line in lines_in:
            parts = line.strip().split()
            if len(parts) >= 5:
                if parts[0] != "0":
                    changed = True
                parts[0] = "0"  # すべて person (class_id=0) に統合
                lines_out.append(" ".join(parts))
        if changed:
            modified += 1
        with open(txt_path, "w") as f:
            f.write("\n".join(lines_out) + "\n" if lines_out else "")
    print(f"  Unified {modified}/{len(txt_files)} label files to person class (id=0)")


def download_and_process(api_key: str, max_images: int):
    """Roboflow からデータセットをダウンロードして処理"""
    print("\n[Step 1/3] Downloading from Roboflow...")
    rf = Roboflow(api_key=api_key)

    # Fall Detection by Roboflow Universe Projects
    # https://universe.roboflow.com/roboflow-universe-projects/fall-detection-ca3o8
    project = rf.workspace("roboflow-universe-projects").project("fall-detection-ca3o8")
    version = project.version(4)
    dataset = version.download("yolov5", location=str(DATASET_ROOT / "downloads" / "roboflow_fall"))

    download_path = Path(dataset.location)
    print(f"  Downloaded to: {download_path}")

    # 2. train/valid/test フォルダの画像・ラベルを統合
    print("\n[Step 2/3] Merging splits and unifying classes...")
    images_dir = OUTPUT_DIR / "images"
    labels_dir = OUTPUT_DIR / "labels"
    images_dir.mkdir(parents=True, exist_ok=True)
    labels_dir.mkdir(parents=True, exist_ok=True)

    total_images = 0
    for split in ["train", "valid", "test"]:
        split_dir = download_path / split
        if not split_dir.exists():
            print(f"  Skipping {split} (not found)")
            continue

        # 画像のコピー
        img_src = split_dir / "images"
        lbl_src = split_dir / "labels"

        if img_src.exists():
            for img_file in img_src.iterdir():
                if img_file.suffix.lower() in (".jpg", ".jpeg", ".png", ".bmp"):
                    dest = images_dir / img_file.name
                    if not dest.exists():
                        shutil.copy2(img_file, dest)
                        total_images += 1

        if lbl_src.exists():
            for lbl_file in lbl_src.iterdir():
                if lbl_file.suffix == ".txt" and lbl_file.name != "classes.txt":
                    dest = labels_dir / lbl_file.name
                    if not dest.exists():
                        shutil.copy2(lbl_file, dest)

    print(f"  Copied {total_images} images")

    # 3. クラス統合
    print("\n[Step 3/3] Unifying classes to person (id=0)...")
    unify_classes(labels_dir)

    # サマリ
    n_images = len(list(images_dir.glob("*")))
    n_labels = len(list(labels_dir.glob("*.txt")))
    print(f"\n  Summary:")
    print(f"    Images: {n_images}")
    print(f"    Labels: {n_labels}")
    print(f"    Output: {OUTPUT_DIR}")

    # classes.txt を作成
    with open(OUTPUT_DIR / "classes.txt", "w") as f:
        f.write("person\n")
    print(f"    classes.txt created")


def main():
    parser = argparse.ArgumentParser(description="Download Roboflow Fall Detection dataset")
    parser.add_argument("--api-key", type=str, default=None,
                        help="Roboflow API key (or set ROBOFLOW_API_KEY env var)")
    parser.add_argument("--max-images", type=int, default=0,
                        help="Maximum images to keep (0=all, default: 0)")
    args = parser.parse_args()

    api_key = args.api_key or os.environ.get("ROBOFLOW_API_KEY")
    if not api_key:
        print("Error: Roboflow API key is required.")
        print("  Option 1: python download_roboflow_fall.py --api-key YOUR_KEY")
        print("  Option 2: set ROBOFLOW_API_KEY=YOUR_KEY && python download_roboflow_fall.py")
        print("")
        print("To get an API key:")
        print("  1. Create a free account at https://app.roboflow.com/")
        print("  2. Go to Settings > API Key")
        return

    download_and_process(api_key, args.max_images)


if __name__ == "__main__":
    main()
