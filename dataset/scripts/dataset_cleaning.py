"""
データセットクリーニングスクリプト (Issue #133 / Phase 2A)

YOLO-Fastest 人物検出モデル学習用の統合済みデータセットに対して
以下のクリーニングを実行する:

  1. 低品質画像の検出・除去
     - ぼけ画像 (Laplacian variance が閾値未満)
     - 暗すぎる画像 (輝度平均が閾値未満)
     - 明るすぎる画像 / 白飛び (輝度平均が閾値超過)
  2. ラベル異常の検出・除去
     - 座標値が [0,1] 範囲外
     - 幅/高さが 0 または極小
     - 画像境界をはみ出す bbox
     - 画像/ラベル片側のみ存在
  3. 重複画像の検出・統合
     - perceptual hash (ahash) による near-duplicate 検出
     - 重複する画像群のうち 1 枚だけを残す
  4. 小人物サンプル統計 (除去はしない、確認用)

本スクリプトは Issue #131 で作成したデータセットを破壊的に変更しないよう、
デフォルトでは dry-run (report のみ) で動作する。
`--apply` を指定した場合のみ、対象ファイルをゴミ箱ディレクトリに移動する。

使い方 (Colab 想定):
  python dataset_cleaning.py \
      --dataset /content/dataset \
      --blur-threshold 50 \
      --dark-threshold 25 \
      --bright-threshold 235 \
      --dup-hash-size 8 \
      --dup-hamming-threshold 4 \
      --apply

使い方 (dry-run / ローカル):
  python dataset_cleaning.py --dataset ./dataset

Notes:
- 本スクリプトは numpy/Pillow のみに依存する。OpenCV が使える環境では
  より高速な blur 検出 (cv2.Laplacian) を自動で使う。
- `--apply` 時、削除対象は `<dataset>/_removed_phase2/` 以下に移動する
  (物理削除しない = 切り戻し可能)
- Ran on Colab after Step 2 (dataset unzip), before Step 3 (cfg generation).
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter, ImageStat

try:
    import cv2  # type: ignore

    HAS_CV2 = True
except ImportError:
    HAS_CV2 = False


IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


# ---------------------------------------------------------------------------
# 基本ユーティリティ
# ---------------------------------------------------------------------------

def iter_images(dataset_dir: Path):
    """dataset/images/{train,val,test}/*.jpg を yield する"""
    for split in ("train", "val", "test"):
        img_dir = dataset_dir / "images" / split
        if not img_dir.is_dir():
            continue
        for p in sorted(img_dir.iterdir()):
            if p.suffix.lower() in IMAGE_EXTENSIONS:
                yield split, p


def label_path_for(image_path: Path, dataset_dir: Path) -> Path:
    """画像パスに対応するラベルパスを返す"""
    split = image_path.parent.name  # train/val/test
    return dataset_dir / "labels" / split / (image_path.stem + ".txt")


def move_to_removed(
    path: Path,
    dataset_dir: Path,
    reason: str,
    removed_root: Path,
) -> None:
    """対象ファイルを _removed_phase2/<reason>/... に移動する"""
    try:
        rel = path.relative_to(dataset_dir)
    except ValueError:
        rel = Path(path.name)
    dst = removed_root / reason / rel
    dst.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        shutil.move(str(path), str(dst))


def remove_pair(
    image_path: Path,
    dataset_dir: Path,
    reason: str,
    removed_root: Path,
    apply: bool,
) -> None:
    """画像とラベルのペアを一括で退避する"""
    label = label_path_for(image_path, dataset_dir)
    if apply:
        move_to_removed(image_path, dataset_dir, reason, removed_root)
        if label.exists():
            move_to_removed(label, dataset_dir, reason, removed_root)


# ---------------------------------------------------------------------------
# Check 1: 低品質画像検出
# ---------------------------------------------------------------------------

def laplacian_variance(image_path: Path) -> float:
    """Laplacian variance (ぼけ指標)。値が小さいほどぼけている。"""
    if HAS_CV2:
        img = cv2.imread(str(image_path), cv2.IMREAD_GRAYSCALE)
        if img is None:
            return -1.0
        return float(cv2.Laplacian(img, cv2.CV_64F).var())
    # Pillow fallback: 近似的な sharpness 指標
    img = Image.open(image_path).convert("L")
    edges = img.filter(ImageFilter.FIND_EDGES)
    arr = np.asarray(edges, dtype=np.float32)
    return float(arr.var())


def brightness_mean(image_path: Path) -> float:
    """輝度の平均値 (0-255)"""
    img = Image.open(image_path).convert("L")
    stat = ImageStat.Stat(img)
    return float(stat.mean[0])


def check_low_quality_images(
    dataset_dir: Path,
    blur_threshold: float,
    dark_threshold: float,
    bright_threshold: float,
) -> dict:
    """低品質画像を検出して候補リストを返す"""
    result = {
        "blur": [],
        "too_dark": [],
        "too_bright": [],
        "unreadable": [],
        "total": 0,
    }
    for _split, path in iter_images(dataset_dir):
        result["total"] += 1
        try:
            lv = laplacian_variance(path)
            br = brightness_mean(path)
        except Exception as e:
            result["unreadable"].append((str(path), str(e)))
            continue
        if lv >= 0 and lv < blur_threshold:
            result["blur"].append((str(path), lv))
        if br < dark_threshold:
            result["too_dark"].append((str(path), br))
        elif br > bright_threshold:
            result["too_bright"].append((str(path), br))
    return result


# ---------------------------------------------------------------------------
# Check 2: ラベル異常検出
# ---------------------------------------------------------------------------

def check_label_issues(dataset_dir: Path) -> dict:
    """ラベル異常 (範囲外, サイズ0, 画像/ラベル対応ミス) を検出する"""
    result = {
        "out_of_range": [],
        "zero_size": [],
        "tiny": [],         # w or h < 0.005 (極小、学習ノイズになりやすい)
        "missing_label": [],
        "missing_image": [],
        "bad_class": [],
        "total_bboxes": 0,
    }

    image_stems_by_split: dict[str, set[str]] = {}
    label_stems_by_split: dict[str, set[str]] = {}

    for split in ("train", "val", "test"):
        img_dir = dataset_dir / "images" / split
        lbl_dir = dataset_dir / "labels" / split
        if not img_dir.is_dir() or not lbl_dir.is_dir():
            continue

        image_stems_by_split[split] = {
            p.stem for p in img_dir.iterdir() if p.suffix.lower() in IMAGE_EXTENSIONS
        }
        label_stems_by_split[split] = {
            p.stem for p in lbl_dir.iterdir() if p.suffix == ".txt"
        }

        for txt_path in sorted(lbl_dir.iterdir()):
            if txt_path.suffix != ".txt":
                continue
            try:
                lines = txt_path.read_text().splitlines()
            except Exception:
                continue
            for line_num, raw in enumerate(lines, 1):
                parts = raw.strip().split()
                if not parts:
                    continue
                if len(parts) != 5:
                    result["out_of_range"].append(
                        f"{split}/{txt_path.name}:{line_num} - not 5 values"
                    )
                    continue
                try:
                    cls = int(parts[0])
                    cx, cy, w, h = (float(v) for v in parts[1:])
                except ValueError:
                    result["out_of_range"].append(
                        f"{split}/{txt_path.name}:{line_num} - non-numeric"
                    )
                    continue
                result["total_bboxes"] += 1
                if cls != 0:
                    result["bad_class"].append(
                        f"{split}/{txt_path.name}:{line_num} - class={cls}"
                    )
                # 範囲外
                if not (0.0 <= cx <= 1.0 and 0.0 <= cy <= 1.0
                        and 0.0 <= w <= 1.0 and 0.0 <= h <= 1.0):
                    result["out_of_range"].append(
                        f"{split}/{txt_path.name}:{line_num} - "
                        f"cx={cx:.3f} cy={cy:.3f} w={w:.3f} h={h:.3f}"
                    )
                # 境界はみ出し
                if (cx - w / 2 < -0.01 or cy - h / 2 < -0.01
                        or cx + w / 2 > 1.01 or cy + h / 2 > 1.01):
                    result["out_of_range"].append(
                        f"{split}/{txt_path.name}:{line_num} - bbox out of image"
                    )
                if w <= 0 or h <= 0:
                    result["zero_size"].append(
                        f"{split}/{txt_path.name}:{line_num} - w={w} h={h}"
                    )
                elif w < 0.005 or h < 0.005:
                    result["tiny"].append(
                        f"{split}/{txt_path.name}:{line_num} - w={w:.4f} h={h:.4f}"
                    )

    # 画像/ラベル対応
    for split in image_stems_by_split:
        img_stems = image_stems_by_split[split]
        lbl_stems = label_stems_by_split.get(split, set())
        for stem in sorted(img_stems - lbl_stems):
            result["missing_label"].append(f"{split}/{stem}")
        for stem in sorted(lbl_stems - img_stems):
            result["missing_image"].append(f"{split}/{stem}")

    return result


# ---------------------------------------------------------------------------
# Check 3: 重複画像検出 (perceptual hash)
# ---------------------------------------------------------------------------

def average_hash(image_path: Path, hash_size: int = 8) -> int:
    """perceptual hash (ahash)。リサイズ + 平均輝度二値化 -> int"""
    img = Image.open(image_path).convert("L").resize(
        (hash_size, hash_size), Image.BILINEAR
    )
    arr = np.asarray(img, dtype=np.float32)
    mean = arr.mean()
    bits = (arr > mean).astype(np.uint8).flatten()
    value = 0
    for b in bits:
        value = (value << 1) | int(b)
    return value


def hamming_distance(a: int, b: int) -> int:
    return bin(a ^ b).count("1")


def check_duplicates(
    dataset_dir: Path,
    hash_size: int = 8,
    hamming_threshold: int = 4,
) -> dict:
    """perceptual hash で画像バケットを作り、near-duplicate を検出する

    計算量を抑えるため、まず完全一致 hash でバケット分けし、その中で
    hamming 距離を比較する。異なるバケット間の near-duplicate は見逃すが、
    >=32k 枚のデータセットに対して現実的な速度で動作する。
    """
    result = {
        "groups": [],  # 各 group = list of image path
        "num_duplicates": 0,
        "total": 0,
    }
    buckets: dict[int, list[Path]] = defaultdict(list)
    for _split, path in iter_images(dataset_dir):
        result["total"] += 1
        try:
            h = average_hash(path, hash_size=hash_size)
        except Exception:
            continue
        # 上位ビットでバケット分け (hamming<=threshold でも上位 hash_size*hash_size - threshold
        # ビットは大体同じ想定)。粗いクラスタリングとして prefix 16bit を使う。
        bucket_key = h >> ((hash_size * hash_size) - 16)
        buckets[bucket_key].append(path)

    for bucket_paths in buckets.values():
        if len(bucket_paths) < 2:
            continue
        # bucket 内で hash を再計算 (キャッシュしても良いが保守的に小バケットで再計算)
        hashes = {p: average_hash(p, hash_size=hash_size) for p in bucket_paths}
        visited: set[Path] = set()
        for i, p in enumerate(bucket_paths):
            if p in visited:
                continue
            group = [p]
            visited.add(p)
            for q in bucket_paths[i + 1:]:
                if q in visited:
                    continue
                if hamming_distance(hashes[p], hashes[q]) <= hamming_threshold:
                    group.append(q)
                    visited.add(q)
            if len(group) > 1:
                result["groups"].append([str(x) for x in group])
                result["num_duplicates"] += len(group) - 1

    return result


# ---------------------------------------------------------------------------
# Summary / Report
# ---------------------------------------------------------------------------

def print_report(
    quality: dict,
    labels: dict,
    duplicates: dict,
    max_preview: int = 5,
) -> None:
    print("\n" + "=" * 60)
    print("Phase 2A Dataset Cleaning Report")
    print("=" * 60)

    print("\n[1] Low quality images")
    print(f"  total images scanned : {quality['total']}")
    print(f"  blur candidates      : {len(quality['blur'])}")
    print(f"  too_dark candidates  : {len(quality['too_dark'])}")
    print(f"  too_bright candidates: {len(quality['too_bright'])}")
    print(f"  unreadable           : {len(quality['unreadable'])}")
    for tag in ("blur", "too_dark", "too_bright"):
        if quality[tag]:
            print(f"  -- {tag} preview --")
            for p, v in quality[tag][:max_preview]:
                print(f"     {Path(p).name}: {v:.2f}")

    print("\n[2] Label issues")
    print(f"  total bboxes checked : {labels['total_bboxes']}")
    print(f"  out_of_range         : {len(labels['out_of_range'])}")
    print(f"  zero_size            : {len(labels['zero_size'])}")
    print(f"  tiny (<0.005)        : {len(labels['tiny'])}")
    print(f"  missing_label        : {len(labels['missing_label'])}")
    print(f"  missing_image        : {len(labels['missing_image'])}")
    print(f"  bad_class            : {len(labels['bad_class'])}")
    for tag in ("out_of_range", "zero_size", "missing_label", "missing_image"):
        if labels[tag]:
            print(f"  -- {tag} preview --")
            for s in labels[tag][:max_preview]:
                print(f"     {s}")

    print("\n[3] Duplicate images (perceptual hash)")
    print(f"  total images hashed  : {duplicates['total']}")
    print(f"  duplicate groups     : {len(duplicates['groups'])}")
    print(f"  redundant images     : {duplicates['num_duplicates']}")
    for group in duplicates["groups"][:max_preview]:
        print(f"  group: {[Path(p).name for p in group]}")

    print("\n" + "=" * 60)


def apply_cleaning(
    dataset_dir: Path,
    quality: dict,
    labels: dict,
    duplicates: dict,
    removed_root: Path,
) -> dict:
    """検出結果を元に、実際のファイル退避を実施する"""
    stats = {
        "removed_blur": 0,
        "removed_dark": 0,
        "removed_bright": 0,
        "removed_bad_label": 0,
        "removed_duplicate": 0,
    }

    removed_paths: set[str] = set()

    # 1) 画像品質NG (画像+ラベルのペアで退避)
    for path_str, _ in quality["blur"]:
        p = Path(path_str)
        if p.exists():
            remove_pair(p, dataset_dir, "blur", removed_root, apply=True)
            removed_paths.add(path_str)
            stats["removed_blur"] += 1
    for path_str, _ in quality["too_dark"]:
        p = Path(path_str)
        if str(p) in removed_paths or not p.exists():
            continue
        remove_pair(p, dataset_dir, "too_dark", removed_root, apply=True)
        removed_paths.add(path_str)
        stats["removed_dark"] += 1
    for path_str, _ in quality["too_bright"]:
        p = Path(path_str)
        if str(p) in removed_paths or not p.exists():
            continue
        remove_pair(p, dataset_dir, "too_bright", removed_root, apply=True)
        removed_paths.add(path_str)
        stats["removed_bright"] += 1

    # 2) ラベル異常: missing_image のラベルファイル、missing_label の画像、
    #    out_of_range/zero_size を含むラベル行 -> ファイルごと退避は過剰になるため
    #    ここでは「完全に壊れているラベルファイル (全行が out_of_range or zero_size)」
    #    と missing 対応のみを退避する。軽微な異常行は darknet 側の clip に任せる。
    def _txt_from_code(code: str) -> Path | None:
        # code 例: "train/img_0001.txt:3 - ..."
        head = code.split(" ", 1)[0]
        rel = head.split(":", 1)[0]
        return dataset_dir / "labels" / rel

    bad_files: dict[Path, int] = defaultdict(int)
    total_lines: dict[Path, int] = defaultdict(int)
    for code in labels["out_of_range"] + labels["zero_size"]:
        lp = _txt_from_code(code)
        if lp is not None:
            bad_files[lp] += 1
    for lbl_dir in (dataset_dir / "labels").glob("*"):
        if not lbl_dir.is_dir():
            continue
        for txt in lbl_dir.glob("*.txt"):
            try:
                total_lines[txt] = sum(
                    1 for ln in txt.read_text().splitlines() if ln.strip()
                )
            except Exception:
                total_lines[txt] = 0

    for lp, bad_count in bad_files.items():
        total = total_lines.get(lp, 0)
        if total == 0 or bad_count >= total:
            # 対応画像を探して pair 退避
            split = lp.parent.name
            stem = lp.stem
            for ext in IMAGE_EXTENSIONS:
                img = dataset_dir / "images" / split / (stem + ext)
                if img.exists():
                    remove_pair(img, dataset_dir, "bad_label", removed_root, apply=True)
                    removed_paths.add(str(img))
                    stats["removed_bad_label"] += 1
                    break
            else:
                move_to_removed(lp, dataset_dir, "bad_label", removed_root)
                stats["removed_bad_label"] += 1

    # missing_image: ラベルのみ存在 -> ラベルを退避
    for rel in labels["missing_image"]:
        split, stem = rel.split("/", 1)
        lp = dataset_dir / "labels" / split / f"{stem}.txt"
        if lp.exists():
            move_to_removed(lp, dataset_dir, "missing_image", removed_root)
            stats["removed_bad_label"] += 1
    # missing_label: 画像のみ存在 -> 画像を退避
    for rel in labels["missing_label"]:
        split, stem = rel.split("/", 1)
        for ext in IMAGE_EXTENSIONS:
            img = dataset_dir / "images" / split / (stem + ext)
            if img.exists():
                move_to_removed(img, dataset_dir, "missing_label", removed_root)
                removed_paths.add(str(img))
                stats["removed_bad_label"] += 1
                break

    # 3) 重複画像: 各グループの先頭を残し、残りを退避
    for group in duplicates["groups"]:
        for path_str in group[1:]:
            p = Path(path_str)
            if str(p) in removed_paths or not p.exists():
                continue
            remove_pair(p, dataset_dir, "duplicate", removed_root, apply=True)
            removed_paths.add(path_str)
            stats["removed_duplicate"] += 1

    return stats


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dataset",
        type=Path,
        default=Path("/content/dataset"),
        help="データセットルート (images/train, labels/train 構造)",
    )
    parser.add_argument("--blur-threshold", type=float, default=50.0,
                        help="Laplacian variance がこの値未満ならぼけ判定")
    parser.add_argument("--dark-threshold", type=float, default=25.0,
                        help="輝度平均がこの値未満なら暗すぎ判定")
    parser.add_argument("--bright-threshold", type=float, default=235.0,
                        help="輝度平均がこの値超過なら白飛び判定")
    parser.add_argument("--dup-hash-size", type=int, default=8,
                        help="perceptual hash のサイズ (8 -> 64bit)")
    parser.add_argument("--dup-hamming-threshold", type=int, default=4,
                        help="hamming 距離がこの値以下なら重複とみなす")
    parser.add_argument("--apply", action="store_true",
                        help="検出結果に基づいて実際にファイルを退避する")
    parser.add_argument("--report", type=Path, default=None,
                        help="レポートを JSON で出力する先 (省略時は stdout のみ)")
    parser.add_argument("--skip-duplicates", action="store_true",
                        help="重複検出をスキップする (大規模データセット向け高速化)")
    args = parser.parse_args()

    dataset_dir: Path = args.dataset.resolve()
    if not (dataset_dir / "images").is_dir():
        print(f"ERROR: {dataset_dir}/images が見つかりません", file=sys.stderr)
        sys.exit(1)

    print(f"Dataset: {dataset_dir}")
    print(f"OpenCV available: {HAS_CV2}")
    print()

    print("[1/3] Scanning image quality ...")
    quality = check_low_quality_images(
        dataset_dir,
        blur_threshold=args.blur_threshold,
        dark_threshold=args.dark_threshold,
        bright_threshold=args.bright_threshold,
    )

    print("[2/3] Scanning label integrity ...")
    labels = check_label_issues(dataset_dir)

    if args.skip_duplicates:
        duplicates = {"groups": [], "num_duplicates": 0, "total": 0}
    else:
        print("[3/3] Scanning duplicates (perceptual hash) ...")
        duplicates = check_duplicates(
            dataset_dir,
            hash_size=args.dup_hash_size,
            hamming_threshold=args.dup_hamming_threshold,
        )

    print_report(quality, labels, duplicates)

    if args.report is not None:
        payload = {
            "dataset": str(dataset_dir),
            "quality": {
                "total": quality["total"],
                "blur_count": len(quality["blur"]),
                "too_dark_count": len(quality["too_dark"]),
                "too_bright_count": len(quality["too_bright"]),
                "unreadable_count": len(quality["unreadable"]),
            },
            "labels": {
                "total_bboxes": labels["total_bboxes"],
                "out_of_range_count": len(labels["out_of_range"]),
                "zero_size_count": len(labels["zero_size"]),
                "tiny_count": len(labels["tiny"]),
                "missing_label_count": len(labels["missing_label"]),
                "missing_image_count": len(labels["missing_image"]),
                "bad_class_count": len(labels["bad_class"]),
            },
            "duplicates": {
                "total": duplicates["total"],
                "group_count": len(duplicates["groups"]),
                "redundant_count": duplicates["num_duplicates"],
            },
        }
        args.report.write_text(json.dumps(payload, indent=2))
        print(f"\nReport saved: {args.report}")

    if args.apply:
        removed_root = dataset_dir / "_removed_phase2"
        removed_root.mkdir(exist_ok=True)
        print(f"\n[APPLY] 退避先: {removed_root}")
        stats = apply_cleaning(dataset_dir, quality, labels, duplicates, removed_root)
        print("\n[APPLY] 退避結果:")
        for k, v in stats.items():
            print(f"  {k:25s}: {v}")
        print("\nNOTE: 退避ファイルは _removed_phase2/ 以下にあります。"
              " 物理削除はされません。")
    else:
        print("\n(dry-run モード: --apply を指定すると実際に退避します)")


if __name__ == "__main__":
    main()
