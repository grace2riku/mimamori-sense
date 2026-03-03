"""
データセット品質チェックスクリプト

統合済みデータセット (dataset/merged/) に対して以下のチェックを実行する:
  1. アノテーション形式の検証 (YOLO形式 5値)
  2. 座標値の範囲チェック (0.0-1.0)
  3. 画像-ラベル対応の整合性チェック
  4. 空ラベルファイルの検出
  5. BBoxサイズの異常値検出
  6. アスペクト比分布の分析
  7. データソース別の内訳

使い方:
  python quality_check.py
"""

import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent
MERGED_DIR = DATASET_ROOT / "merged"

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


def check_annotation_format(labels_dir: Path) -> dict:
    """チェック1: アノテーション形式の検証"""
    results = {
        "total_files": 0,
        "total_bboxes": 0,
        "format_errors": [],
        "non_zero_class": [],
    }

    for txt_path in sorted(labels_dir.rglob("*.txt")):
        results["total_files"] += 1
        with open(txt_path) as f:
            for line_num, line in enumerate(f, 1):
                line = line.strip()
                if not line:
                    continue
                parts = line.split()
                if len(parts) != 5:
                    results["format_errors"].append(
                        f"{txt_path.name}:{line_num} - {len(parts)} values (expected 5)"
                    )
                    continue
                results["total_bboxes"] += 1
                if parts[0] != "0":
                    results["non_zero_class"].append(
                        f"{txt_path.name}:{line_num} - class_id={parts[0]}"
                    )

    return results


def check_coordinate_range(labels_dir: Path) -> dict:
    """チェック2: 座標値の範囲チェック (0.0-1.0)"""
    results = {"out_of_range": [], "total_checked": 0}

    for txt_path in sorted(labels_dir.rglob("*.txt")):
        with open(txt_path) as f:
            for line_num, line in enumerate(f, 1):
                parts = line.strip().split()
                if len(parts) != 5:
                    continue
                results["total_checked"] += 1
                values = [float(v) for v in parts[1:]]
                for i, v in enumerate(values):
                    coord_name = ["x_center", "y_center", "width", "height"][i]
                    if v < 0.0 or v > 1.0:
                        results["out_of_range"].append(
                            f"{txt_path.name}:{line_num} - {coord_name}={v:.6f}"
                        )

    return results


def check_image_label_correspondence(merged_dir: Path) -> dict:
    """チェック3: 画像-ラベル対応の整合性チェック"""
    results = {"missing_labels": [], "missing_images": [], "matched": 0}

    for split in ["train", "val", "test"]:
        img_dir = merged_dir / "images" / split
        lbl_dir = merged_dir / "labels" / split

        if not img_dir.exists():
            continue

        img_stems = {
            p.stem for p in img_dir.iterdir() if p.suffix.lower() in IMAGE_EXTENSIONS
        }
        lbl_stems = {p.stem for p in lbl_dir.iterdir() if p.suffix == ".txt"}

        for stem in img_stems - lbl_stems:
            results["missing_labels"].append(f"{split}/{stem}")
        for stem in lbl_stems - img_stems:
            results["missing_images"].append(f"{split}/{stem}")
        results["matched"] += len(img_stems & lbl_stems)

    return results


def check_empty_labels(labels_dir: Path) -> dict:
    """チェック4: 空ラベルファイルの検出"""
    results = {"empty_files": [], "total_files": 0}

    for txt_path in sorted(labels_dir.rglob("*.txt")):
        results["total_files"] += 1
        content = txt_path.read_text().strip()
        if not content:
            results["empty_files"].append(str(txt_path.relative_to(labels_dir)))

    return results


def check_bbox_size(labels_dir: Path) -> dict:
    """チェック5: BBoxサイズの異常値検出"""
    results = {"tiny_bboxes": [], "huge_bboxes": [], "total_checked": 0}

    for txt_path in sorted(labels_dir.rglob("*.txt")):
        with open(txt_path) as f:
            for line_num, line in enumerate(f, 1):
                parts = line.strip().split()
                if len(parts) != 5:
                    continue
                results["total_checked"] += 1
                w, h = float(parts[3]), float(parts[4])
                if w < 0.01 or h < 0.01:
                    results["tiny_bboxes"].append(
                        f"{txt_path.name}:{line_num} - w={w:.4f}, h={h:.4f}"
                    )
                if w > 0.95 or h > 0.95:
                    results["huge_bboxes"].append(
                        f"{txt_path.name}:{line_num} - w={w:.4f}, h={h:.4f}"
                    )

    return results


def analyze_aspect_ratios(labels_dir: Path) -> dict:
    """チェック6: アスペクト比分布の分析"""
    aspect_ratios = []
    bbox_per_image = []

    for txt_path in sorted(labels_dir.rglob("*.txt")):
        count = 0
        with open(txt_path) as f:
            for line in f:
                parts = line.strip().split()
                if len(parts) != 5:
                    continue
                w, h = float(parts[3]), float(parts[4])
                if h > 0:
                    aspect_ratios.append(w / h)
                count += 1
        bbox_per_image.append(count)

    if not aspect_ratios:
        return {"error": "No bounding boxes found"}

    ar = sorted(aspect_ratios)
    n = len(ar)

    # アスペクト比の統計
    portrait = sum(1 for r in ar if r < 1.0)  # 縦長 (立位/歩行)
    landscape = sum(1 for r in ar if r >= 1.0)  # 横長 (転倒候補)

    results = {
        "total_bboxes": n,
        "ar_min": ar[0],
        "ar_max": ar[-1],
        "ar_median": ar[n // 2],
        "ar_mean": sum(ar) / n,
        "portrait_count": portrait,
        "portrait_pct": portrait / n * 100,
        "landscape_count": landscape,
        "landscape_pct": landscape / n * 100,
        "bbox_per_image_min": min(bbox_per_image),
        "bbox_per_image_max": max(bbox_per_image),
        "bbox_per_image_mean": sum(bbox_per_image) / len(bbox_per_image),
    }

    # アスペクト比のヒストグラム (テキスト版)
    bins = [0, 0.3, 0.5, 0.7, 0.9, 1.0, 1.1, 1.3, 1.5, 2.0, 3.0, float("inf")]
    bin_labels = [
        "0.0-0.3", "0.3-0.5", "0.5-0.7", "0.7-0.9", "0.9-1.0",
        "1.0-1.1", "1.1-1.3", "1.3-1.5", "1.5-2.0", "2.0-3.0", "3.0+"
    ]
    histogram = []
    for i in range(len(bins) - 1):
        count = sum(1 for r in ar if bins[i] <= r < bins[i + 1])
        histogram.append((bin_labels[i], count))
    results["histogram"] = histogram

    return results


def check_source_breakdown(merged_dir: Path) -> dict:
    """チェック7: データソース別の内訳"""
    results = {}

    for split in ["train", "val", "test"]:
        img_dir = merged_dir / "images" / split
        if not img_dir.exists():
            continue
        coco = 0
        rf = 0
        other = 0
        for p in img_dir.iterdir():
            if p.suffix.lower() not in IMAGE_EXTENSIONS:
                continue
            if p.stem.startswith("coco_"):
                coco += 1
            elif p.stem.startswith("rf_"):
                rf += 1
            else:
                other += 1
        results[split] = {"coco": coco, "roboflow": rf, "other": other}

    return results


def main():
    print("=" * 60)
    print("Dataset Quality Check Report")
    print("=" * 60)

    if not MERGED_DIR.exists():
        print(f"Error: {MERGED_DIR} not found. Run merge_and_split.py first.")
        sys.exit(1)

    labels_dir = MERGED_DIR / "labels"
    all_pass = True

    # チェック1: アノテーション形式
    print("\n--- Check 1: Annotation Format ---")
    r = check_annotation_format(labels_dir)
    print(f"  Total label files: {r['total_files']}")
    print(f"  Total bounding boxes: {r['total_bboxes']}")
    if r["format_errors"]:
        print(f"  FAIL: {len(r['format_errors'])} format errors")
        for e in r["format_errors"][:10]:
            print(f"    {e}")
        all_pass = False
    else:
        print("  PASS: All annotations are valid YOLO format (5 values)")
    if r["non_zero_class"]:
        print(f"  WARNING: {len(r['non_zero_class'])} bboxes with non-zero class_id")
        for e in r["non_zero_class"][:5]:
            print(f"    {e}")
    else:
        print("  PASS: All bboxes use class_id=0 (person)")

    # チェック2: 座標値の範囲
    print("\n--- Check 2: Coordinate Range (0.0-1.0) ---")
    r = check_coordinate_range(labels_dir)
    print(f"  Total checked: {r['total_checked']} bboxes")
    if r["out_of_range"]:
        print(f"  FAIL: {len(r['out_of_range'])} out-of-range values")
        for e in r["out_of_range"][:10]:
            print(f"    {e}")
        all_pass = False
    else:
        print("  PASS: All coordinates within [0.0, 1.0]")

    # チェック3: 画像-ラベル対応
    print("\n--- Check 3: Image-Label Correspondence ---")
    r = check_image_label_correspondence(MERGED_DIR)
    print(f"  Matched pairs: {r['matched']}")
    if r["missing_labels"]:
        print(f"  FAIL: {len(r['missing_labels'])} images without labels")
        for e in r["missing_labels"][:5]:
            print(f"    {e}")
        all_pass = False
    else:
        print("  PASS: All images have corresponding labels")
    if r["missing_images"]:
        print(f"  FAIL: {len(r['missing_images'])} labels without images")
        for e in r["missing_images"][:5]:
            print(f"    {e}")
        all_pass = False
    else:
        print("  PASS: All labels have corresponding images")

    # チェック4: 空ラベルファイル
    print("\n--- Check 4: Empty Label Files ---")
    r = check_empty_labels(labels_dir)
    print(f"  Total label files: {r['total_files']}")
    if r["empty_files"]:
        print(f"  WARNING: {len(r['empty_files'])} empty label files")
        for e in r["empty_files"][:10]:
            print(f"    {e}")
    else:
        print("  PASS: No empty label files")

    # チェック5: BBoxサイズ
    print("\n--- Check 5: BBox Size Anomalies ---")
    r = check_bbox_size(labels_dir)
    print(f"  Total checked: {r['total_checked']} bboxes")
    if r["tiny_bboxes"]:
        print(f"  WARNING: {len(r['tiny_bboxes'])} tiny bboxes (w or h < 0.01)")
        for e in r["tiny_bboxes"][:5]:
            print(f"    {e}")
    else:
        print("  PASS: No tiny bboxes")
    if r["huge_bboxes"]:
        print(f"  WARNING: {len(r['huge_bboxes'])} huge bboxes (w or h > 0.95)")
        for e in r["huge_bboxes"][:5]:
            print(f"    {e}")
    else:
        print("  PASS: No huge bboxes")

    # チェック6: アスペクト比分布
    print("\n--- Check 6: Aspect Ratio Distribution ---")
    r = analyze_aspect_ratios(labels_dir)
    if "error" in r:
        print(f"  ERROR: {r['error']}")
    else:
        print(f"  Total bboxes: {r['total_bboxes']}")
        print(f"  Aspect ratio (w/h): min={r['ar_min']:.3f}, max={r['ar_max']:.3f}, "
              f"median={r['ar_median']:.3f}, mean={r['ar_mean']:.3f}")
        print(f"  Portrait (w/h < 1.0): {r['portrait_count']} ({r['portrait_pct']:.1f}%)")
        print(f"  Landscape (w/h >= 1.0): {r['landscape_count']} ({r['landscape_pct']:.1f}%)")
        print(f"  BBox per image: min={r['bbox_per_image_min']}, "
              f"max={r['bbox_per_image_max']}, mean={r['bbox_per_image_mean']:.1f}")

        print("\n  Aspect Ratio Histogram:")
        max_count = max(c for _, c in r["histogram"])
        bar_width = 40
        for label, count in r["histogram"]:
            bar_len = int(count / max_count * bar_width) if max_count > 0 else 0
            print(f"    {label:>8s} | {'#' * bar_len} {count}")

    # チェック7: データソース別内訳
    print("\n--- Check 7: Source Breakdown ---")
    r = check_source_breakdown(MERGED_DIR)
    for split, counts in r.items():
        total = counts["coco"] + counts["roboflow"] + counts["other"]
        print(f"  {split:5s}: COCO={counts['coco']}, Roboflow={counts['roboflow']}"
              + (f", Other={counts['other']}" if counts["other"] else "")
              + f"  (total={total})")

    # 最終結果
    print("\n" + "=" * 60)
    if all_pass:
        print("RESULT: ALL CHECKS PASSED")
    else:
        print("RESULT: SOME CHECKS FAILED - review above")
    print("=" * 60)


if __name__ == "__main__":
    main()
