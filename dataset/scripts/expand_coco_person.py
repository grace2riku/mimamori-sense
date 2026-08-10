"""
COCO 2017 からの人物データ追加取り込みスクリプト (Issue #148 / 案G-②)

Issue #137 (Phase 3) で Recall が 49-52% で頭打ちになり、hard negative 調整では
トレードオフ・フロンティア上を滑るだけで Recall >= 60% に到達できないことが確定した。
本スクリプトは「フロンティアそのものを押し上げる」ための **データ量増加** を担当する。

download_coco_person.py が使っていない COCO 画像 (val2017 全体 + train2017 の未使用分)
から人物画像を選び、**倒れ姿勢 (横長 bbox) を含む画像を優先**して
`dataset/merged/images/train` + `labels/train` に追加する。

--------------------------------------------------------------------------
既存スクリプトとの関係 (重要)
--------------------------------------------------------------------------
- `download_coco_person.py` は **instances_train2017.json のみ**を読み、
  そこから `--max-images` (既定 5000) 枚をランダム抽出して
  `dataset/coco_person/` に置く。val2017 は一切使っていないため、
  val2017 は丸ごと未使用の追加候補になる。
- `merge_and_split.py` は `dataset/merged` を **毎回 rmtree して作り直し**、
  全ペアを `random.shuffle` して train/val/test に振り直す。
  したがってデータ追加後に merge_and_split.py を再実行すると
  **既存の train/val/test 境界が壊れ、過去ラウンドとの評価比較ができなくなる**
  (以前 val だった画像が train に入る = リーク)。
  本スクリプトは merge_and_split.py を呼ばず、**merged/train にのみ追記**し、
  val/test には一切触れない。これにより評価条件 (val) が固定される。
- 取り込んだ原本は `dataset/coco_person_ext/` に残す (切り戻し・再現用)。
  merge_and_split.py はこのディレクトリを読まないため、
  データセットをゼロから作り直す場合は本スクリプトを再実行すること。

--------------------------------------------------------------------------
リーク防止 (同一画像が split をまたがないこと)
--------------------------------------------------------------------------
1. 既に merged のいずれかの split に入っている COCO 画像は候補から除外する。
   merged のファイル名から接頭辞 (coco_ / rf_ / cocoext_ / hardneg_) と
   augment_offline.py が付ける `_aug<N>` を取り除いた「元 stem」で照合する。
2. COCO の画像 ID は train2017 / val2017 で重複しないため、
   ID が違えば別画像だが、**別 ID の near-duplicate** が val/test にある可能性はある。
   `--apply` 時に perceptual hash (ahash) で val/test 画像と照合し、
   hamming 距離が `--dedup-hamming` 以下の候補は破棄する。
3. 追加先は常に `--target-split` (既定 train) の 1 つだけ。

--------------------------------------------------------------------------
空ラベルの扱い
--------------------------------------------------------------------------
- 人物アノテーションはあるが `--min-bbox-px` で全 bbox が落ちた画像は、
  **空ラベルにせず候補から除外**する (意図しない負例の混入を防ぐ)。
  除外件数は `skipped_empty_after_filter` としてレポートに出る。
- 人物が写っていない COCO 画像を明示的に負例として追加したい場合のみ
  `--allow-empty --max-negatives N` を指定する。この場合は空の `.txt` を
  必ず生成する (画像とラベルの 1:1 対応が崩れると
  colab_cli/setup_colab.py の展開時検証で弾かれるため)。

--------------------------------------------------------------------------
使い方
--------------------------------------------------------------------------
  # dry-run (ネットワークアクセスなし。annotations の解析と候補選定のみ)
  python expand_coco_person.py --coco-split both --max-add 5000

  # 実行 (画像ダウンロード + merged/train へ追加)
  python expand_coco_person.py --coco-split both --max-add 5000 \
      --fallen-ratio 0.5 --report ../../issue148_expand_report.json --apply

  # 追加後、Colab へ渡す zip を作り直す (dataset/merged で実行)
  #   zip -r fall_detection_dataset_v2.zip images/ labels/
  #   python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]); print('OK')" \
  #       fall_detection_dataset_v2.zip

Notes:
- annotations (instances_*.json) は `dataset/downloads/coco/annotations/` にある
  前提 (download_coco_person.py が展開済み)。無い場合は
  `--download-annotations` で annotations_trainval2017.zip を取得する。
- 画像は COCO の個別 URL (`coco_url`) から 1 枚ずつ取得する
  (download_coco_person.py と同じ方式)。zip 一括取得はしない。
"""

from __future__ import annotations

import argparse
import json
import random
import shutil
import sys
import zipfile
from pathlib import Path


SCRIPT_DIR = Path(__file__).resolve().parent
DATASET_ROOT = SCRIPT_DIR.parent

COCO_ANNOTATIONS_URL = (
    "http://images.cocodataset.org/annotations/annotations_trainval2017.zip"
)
COCO_PERSON_CATEGORY_ID = 1
YOLO_PERSON_CLASS_ID = 0

IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}
# merged 上のファイル名に付いている接頭辞 (merge_and_split.py / 本スクリプト /
# hard_negative_mining.py が付与する)
MERGED_PREFIXES = ("coco_", "cocoext_", "rf_", "hardneg_")
# 本スクリプトが追加する画像の接頭辞
EXT_PREFIX = "cocoext_"


# ---------------------------------------------------------------------------
# ユーティリティ
# ---------------------------------------------------------------------------

def strip_merged_name(stem: str) -> str:
    """merged のファイル名から接頭辞と _aug<N> を除いた元 stem を返す。"""
    for pre in MERGED_PREFIXES:
        if stem.startswith(pre):
            stem = stem[len(pre):]
            break
    # augment_offline.py: f"{img_path.stem}_aug{i}"
    idx = stem.rfind("_aug")
    if idx > 0 and stem[idx + 4:].isdigit():
        stem = stem[:idx]
    return stem


def coco_bbox_to_yolo(bbox, img_w, img_h):
    """COCO bbox [x_min, y_min, w, h] -> YOLO [xc, yc, w, h] (正規化, clamp)。

    download_coco_person.py の coco_bbox_to_yolo と同一の変換。
    """
    x_min, y_min, bw, bh = bbox
    x_center = (x_min + bw / 2) / img_w
    y_center = (y_min + bh / 2) / img_h
    w_norm = bw / img_w
    h_norm = bh / img_h
    x_center = max(0.0, min(1.0, x_center))
    y_center = max(0.0, min(1.0, y_center))
    w_norm = max(0.0, min(1.0, w_norm))
    h_norm = max(0.0, min(1.0, h_norm))
    return x_center, y_center, w_norm, h_norm


def average_hash(image_path: Path, hash_size: int = 8) -> int:
    """perceptual hash (ahash)。dataset_cleaning.py と同じ定義。"""
    import numpy as np
    from PIL import Image

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


# ---------------------------------------------------------------------------
# 既存データセットの把握
# ---------------------------------------------------------------------------

def scan_merged(merged_dir: Path):
    """merged の split ごとの画像 stem と「元 stem」集合を返す。"""
    per_split = {}
    used_base = set()
    for split in ("train", "val", "test"):
        img_dir = merged_dir / "images" / split
        if not img_dir.is_dir():
            per_split[split] = 0
            continue
        n = 0
        for p in img_dir.iterdir():
            if p.suffix.lower() not in IMAGE_EXTENSIONS:
                continue
            n += 1
            used_base.add(strip_merged_name(p.stem))
        per_split[split] = n
    return per_split, used_base


# ---------------------------------------------------------------------------
# 候補選定
# ---------------------------------------------------------------------------

def load_candidates(
    ann_path: Path,
    used_base: set,
    min_bbox_px: float,
    fallen_ar: float,
):
    """instances_*.json から追加候補を作る。

    返り値: (candidates, negatives, stats)
      candidates: [{stem, file_name, coco_url, width, height, boxes, has_fallen, max_ar}]
      negatives : 人物アノテーションが一切ない画像 (空ラベル候補)
      stats     : dict
    """
    stats = {
        "images_in_annotations": 0,
        "images_with_person": 0,
        "skipped_already_used": 0,
        "skipped_empty_after_filter": 0,
        "candidates": 0,
        "candidates_with_fallen": 0,
        "negative_pool": 0,
    }

    with open(ann_path, encoding="utf-8") as f:
        coco = json.load(f)

    images_map = {img["id"]: img for img in coco["images"]}
    stats["images_in_annotations"] = len(images_map)

    person_anns: dict[int, list] = {}
    any_person: set = set()
    for ann in coco["annotations"]:
        if ann["category_id"] != COCO_PERSON_CATEGORY_ID:
            continue
        img_id = ann["image_id"]
        any_person.add(img_id)
        if ann.get("iscrowd", 0) == 1:
            continue
        _, _, bw, bh = ann["bbox"]
        if bw < min_bbox_px or bh < min_bbox_px:
            continue
        person_anns.setdefault(img_id, []).append(ann)

    stats["images_with_person"] = len(any_person)

    candidates = []
    for img_id in sorted(any_person):
        info = images_map.get(img_id)
        if info is None:
            continue
        stem = Path(info["file_name"]).stem
        if stem in used_base:
            stats["skipped_already_used"] += 1
            continue
        anns = person_anns.get(img_id, [])
        if not anns:
            # 人物はいるが全 bbox が min_bbox_px 未満 -> 空ラベルにはせず除外
            stats["skipped_empty_after_filter"] += 1
            continue
        img_w, img_h = info["width"], info["height"]
        boxes = []
        max_ar = 0.0
        for ann in anns:
            _, _, bw, bh = ann["bbox"]
            ar = bw / bh if bh > 0 else 0.0
            max_ar = max(max_ar, ar)
            boxes.append(coco_bbox_to_yolo(ann["bbox"], img_w, img_h))
        has_fallen = max_ar >= fallen_ar
        candidates.append({
            "stem": stem,
            "file_name": info["file_name"],
            "coco_url": info.get("coco_url", ""),
            "width": img_w,
            "height": img_h,
            "boxes": boxes,
            "has_fallen": has_fallen,
            "max_ar": max_ar,
        })
        if has_fallen:
            stats["candidates_with_fallen"] += 1
    stats["candidates"] = len(candidates)

    negatives = []
    for img_id, info in sorted(images_map.items()):
        if img_id in any_person:
            continue
        stem = Path(info["file_name"]).stem
        if stem in used_base:
            continue
        negatives.append({
            "stem": stem,
            "file_name": info["file_name"],
            "coco_url": info.get("coco_url", ""),
            "width": info["width"],
            "height": info["height"],
            "boxes": [],
            "has_fallen": False,
            "max_ar": 0.0,
        })
    stats["negative_pool"] = len(negatives)

    return candidates, negatives, stats


def select_candidates(candidates, max_add: int, fallen_ratio: float, seed: int):
    """倒れ姿勢を優先しつつ max_add 件を選ぶ。"""
    rng = random.Random(seed)
    fallen = [c for c in candidates if c["has_fallen"]]
    others = [c for c in candidates if not c["has_fallen"]]
    rng.shuffle(fallen)
    rng.shuffle(others)

    n_fallen_target = int(max_add * fallen_ratio)
    take_fallen = fallen[:n_fallen_target]
    remain = max_add - len(take_fallen)
    take_other = others[:remain]
    # other が足りなければ fallen で埋める (逆も同様)
    remain = max_add - len(take_fallen) - len(take_other)
    if remain > 0:
        take_fallen += fallen[len(take_fallen):len(take_fallen) + remain]
    selected = take_fallen + take_other
    rng.shuffle(selected)
    return selected


# ---------------------------------------------------------------------------
# 適用 (ダウンロード + merged への追記)
# ---------------------------------------------------------------------------

def build_eval_hashes(merged_dir: Path, hash_size: int):
    """val/test の画像 ahash を集める (near-duplicate リーク検出用)。"""
    hashes = []
    for split in ("val", "test"):
        img_dir = merged_dir / "images" / split
        if not img_dir.is_dir():
            continue
        for p in sorted(img_dir.iterdir()):
            if p.suffix.lower() not in IMAGE_EXTENSIONS:
                continue
            try:
                hashes.append(average_hash(p, hash_size=hash_size))
            except Exception:
                continue
    return hashes


def download_image(url: str, dest: Path, timeout: int = 30) -> bool:
    import requests

    try:
        r = requests.get(url, timeout=timeout)
        r.raise_for_status()
        dest.parent.mkdir(parents=True, exist_ok=True)
        with open(dest, "wb") as f:
            f.write(r.content)
        return True
    except Exception as e:
        print(f"  Error downloading {dest.name}: {e}")
        return False


def apply_expansion(
    selected,
    merged_dir: Path,
    source_out: Path,
    target_split: str,
    eval_hashes,
    dedup_hamming: int,
    hash_size: int,
):
    """選定済み候補を取得し merged/<target_split> に追加する。"""
    src_img_dir = source_out / "images"
    src_lbl_dir = source_out / "labels"
    dst_img_dir = merged_dir / "images" / target_split
    dst_lbl_dir = merged_dir / "labels" / target_split
    for d in (src_img_dir, src_lbl_dir, dst_img_dir, dst_lbl_dir):
        d.mkdir(parents=True, exist_ok=True)

    result = {
        "downloaded": 0,
        "reused_cache": 0,
        "download_failed": 0,
        "dropped_near_duplicate": 0,
        "already_in_target": 0,
        "added": 0,
        "added_negatives": 0,
    }
    added_names = []

    total = len(selected)
    for i, c in enumerate(selected, 1):
        if i % 200 == 0 or i == total:
            print(f"  [{i}/{total}] added={result['added']} "
                  f"dup={result['dropped_near_duplicate']} "
                  f"fail={result['download_failed']}")

        src_img = src_img_dir / c["file_name"]
        if src_img.exists():
            result["reused_cache"] += 1
        else:
            if not c["coco_url"]:
                result["download_failed"] += 1
                continue
            if not download_image(c["coco_url"], src_img):
                result["download_failed"] += 1
                continue
            result["downloaded"] += 1

        # near-duplicate 検査 (val/test に似た画像を train に入れない)
        if eval_hashes:
            try:
                h = average_hash(src_img, hash_size=hash_size)
            except Exception:
                h = None
            if h is not None and any(
                hamming_distance(h, e) <= dedup_hamming for e in eval_hashes
            ):
                result["dropped_near_duplicate"] += 1
                # 破棄した画像は source_out/images にダウンロード済みのまま残るが、
                # 対応するラベルを書かないため merged には入らない。
                # (merge_and_split.py の collect_pairs もラベルの無い画像は無視する)
                continue

        # 原本ラベル (COCO 名) を保存
        src_lbl = src_lbl_dir / (c["stem"] + ".txt")
        lines = [
            f"{YOLO_PERSON_CLASS_ID} {b[0]:.6f} {b[1]:.6f} {b[2]:.6f} {b[3]:.6f}"
            for b in c["boxes"]
        ]
        src_lbl.write_text(("\n".join(lines) + "\n") if lines else "", encoding="utf-8")

        # merged へ配置 (接頭辞付きで衝突回避)
        new_stem = EXT_PREFIX + c["stem"]
        ext = Path(c["file_name"]).suffix.lower()
        if ext not in IMAGE_EXTENSIONS:
            ext = ".jpg"
        dst_img = dst_img_dir / (new_stem + ext)
        dst_lbl = dst_lbl_dir / (new_stem + ".txt")
        if dst_img.exists():
            result["already_in_target"] += 1
            continue
        shutil.copy2(src_img, dst_img)
        # 空ラベルでも .txt は必ず作る (画像とラベルの 1:1 対応を保つ)
        shutil.copy2(src_lbl, dst_lbl)
        result["added"] += 1
        if not c["boxes"]:
            result["added_negatives"] += 1
        added_names.append(dst_img.name)

    return result, added_names


def regenerate_split_list(merged_dir: Path, split: str, out_name: str):
    """merged/<out_name> を images/<split> から再生成する。

    merge_and_split.py の write_path_list と同じく、絶対パスをソートして書く。
    """
    img_dir = merged_dir / "images" / split
    paths = sorted(
        str(p) for p in img_dir.iterdir()
        if p.suffix.lower() in IMAGE_EXTENSIONS
    )
    out = merged_dir / out_name
    with open(out, "w") as f:
        for p in paths:
            f.write(p + "\n")
    return len(paths), out


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def ensure_annotations(ann_dir: Path, download: bool):
    if ann_dir.is_dir() and any(ann_dir.glob("instances_*2017.json")):
        return
    if not download:
        print(f"ERROR: annotations が見つかりません: {ann_dir}", file=sys.stderr)
        print("  download_coco_person.py を先に実行するか "
              "--download-annotations を指定してください", file=sys.stderr)
        sys.exit(1)
    from urllib.request import urlretrieve

    zip_path = ann_dir.parent / "annotations_trainval2017.zip"
    if not zip_path.exists():
        print(f"annotations をダウンロード中: {COCO_ANNOTATIONS_URL}")
        zip_path.parent.mkdir(parents=True, exist_ok=True)
        urlretrieve(COCO_ANNOTATIONS_URL, str(zip_path))
    print(f"展開中: {zip_path}")
    with zipfile.ZipFile(zip_path) as zf:
        zf.extractall(ann_dir.parent)


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--merged", type=Path, default=DATASET_ROOT / "merged",
                        help="統合済みデータセットのルート (既定: dataset/merged)")
    parser.add_argument("--source-out", type=Path,
                        default=DATASET_ROOT / "coco_person_ext",
                        help="追加取り込み分の原本置き場 (既定: dataset/coco_person_ext)")
    parser.add_argument("--annotations-dir", type=Path,
                        default=DATASET_ROOT / "downloads" / "coco" / "annotations",
                        help="instances_*2017.json のあるディレクトリ")
    parser.add_argument("--download-annotations", action="store_true",
                        help="annotations が無い場合にダウンロードする")
    parser.add_argument("--coco-split", choices=("val2017", "train2017", "both"),
                        default="both",
                        help="候補を採る COCO split (既定: both)")
    parser.add_argument("--target-split", type=str, default="train",
                        help="追加先 split。既定 train。"
                             "val/test を指定すると評価条件が変わるため非推奨")
    parser.add_argument("--max-add", type=int, default=5000,
                        help="追加する画像の上限枚数 (既定: 5000)")
    parser.add_argument("--fallen-ratio", type=float, default=0.5,
                        help="追加分のうち倒れ姿勢 (横長 bbox) を含む画像の目標比率")
    parser.add_argument("--fallen-ar", type=float, default=1.2,
                        help="bbox の w/h がこの値以上なら倒れ姿勢とみなす (ピクセル基準)")
    parser.add_argument("--min-bbox-px", type=float, default=10.0,
                        help="幅または高さがこの値未満の bbox は捨てる "
                             "(download_coco_person.py と同じ既定値)")
    parser.add_argument("--allow-empty", action="store_true",
                        help="人物のいない画像を空ラベルの負例として追加する")
    parser.add_argument("--max-negatives", type=int, default=0,
                        help="--allow-empty 時に追加する負例の枚数")
    parser.add_argument("--dedup-hamming", type=int, default=4,
                        help="val/test 画像との ahash hamming 距離がこの値以下なら破棄")
    parser.add_argument("--dedup-hash-size", type=int, default=8,
                        help="ahash のサイズ (8 -> 64bit)")
    parser.add_argument("--skip-dedup", action="store_true",
                        help="val/test との near-duplicate 検査を省略する (非推奨)")
    parser.add_argument("--seed", type=int, default=42, help="乱数シード")
    parser.add_argument("--apply", action="store_true",
                        help="実際にダウンロードして merged に追加する "
                             "(未指定時は dry-run: 通信なし)")
    parser.add_argument("--report", type=Path, default=None,
                        help="レポート JSON の出力先")
    args = parser.parse_args()

    merged_dir: Path = args.merged.resolve()
    if not (merged_dir / "images").is_dir():
        print(f"ERROR: {merged_dir}/images が見つかりません", file=sys.stderr)
        print("  先に merge_and_split.py を実行してください", file=sys.stderr)
        sys.exit(1)
    if args.target_split != "train":
        print(f"WARNING: --target-split={args.target_split} は評価条件 (val/test) を"
              " 変えるため、過去ラウンドとの比較ができなくなります")

    ensure_annotations(args.annotations_dir, args.download_annotations)

    print(f"merged      : {merged_dir}")
    print(f"source-out  : {args.source_out}")
    print(f"coco-split  : {args.coco_split}")
    print(f"max-add     : {args.max_add} (fallen 目標比率 {args.fallen_ratio:.0%})")
    print(f"mode        : {'APPLY' if args.apply else 'dry-run (通信なし)'}")

    print("\n[1/4] 既存 merged を走査中 ...")
    per_split, used_base = scan_merged(merged_dir)
    for k, v in per_split.items():
        print(f"  {k:5s}: {v} 画像")
    print(f"  既使用の元 stem: {len(used_base)}")

    print("\n[2/4] COCO annotations を解析中 ...")
    ann_files = []
    if args.coco_split in ("val2017", "both"):
        ann_files.append(args.annotations_dir / "instances_val2017.json")
    if args.coco_split in ("train2017", "both"):
        ann_files.append(args.annotations_dir / "instances_train2017.json")

    all_candidates = []
    all_negatives = []
    ann_stats = {}
    for ap in ann_files:
        if not ap.exists():
            print(f"  WARNING: {ap} が見つかりません。スキップします")
            continue
        print(f"  読み込み: {ap.name}")
        cands, negs, st = load_candidates(
            ap, used_base,
            min_bbox_px=args.min_bbox_px,
            fallen_ar=args.fallen_ar,
        )
        ann_stats[ap.name] = st
        all_candidates.extend(cands)
        all_negatives.extend(negs)
        print(f"    画像 {st['images_in_annotations']} / 人物あり {st['images_with_person']}"
              f" / 既使用除外 {st['skipped_already_used']}"
              f" / bbox 全落ち除外 {st['skipped_empty_after_filter']}")
        print(f"    候補 {st['candidates']} (うち倒れ姿勢を含む {st['candidates_with_fallen']})")

    if not all_candidates:
        print("\nERROR: 追加候補が 0 件です。--coco-split や --min-bbox-px を確認してください",
              file=sys.stderr)
        sys.exit(1)

    print("\n[3/4] 候補を選定中 ...")
    selected = select_candidates(
        all_candidates, args.max_add, args.fallen_ratio, args.seed
    )
    n_fallen = sum(1 for c in selected if c["has_fallen"])
    n_boxes = sum(len(c["boxes"]) for c in selected)
    print(f"  選定    : {len(selected)} 画像 / bbox {n_boxes}")
    print(f"  倒れ姿勢を含む: {n_fallen} 画像 "
          f"({n_fallen/len(selected)*100:.1f}%, 目標 {args.fallen_ratio:.0%})")

    selected_negatives = []
    if args.allow_empty and args.max_negatives > 0:
        rng = random.Random(args.seed + 1)
        rng.shuffle(all_negatives)
        selected_negatives = all_negatives[:args.max_negatives]
        print(f"  負例 (空ラベル): {len(selected_negatives)} 画像")

    summary = {
        "issue": "#148",
        "merged": str(merged_dir),
        "source_out": str(args.source_out),
        "coco_split": args.coco_split,
        "target_split": args.target_split,
        "params": {
            "max_add": args.max_add,
            "fallen_ratio": args.fallen_ratio,
            "fallen_ar": args.fallen_ar,
            "min_bbox_px": args.min_bbox_px,
            "dedup_hamming": None if args.skip_dedup else args.dedup_hamming,
            "seed": args.seed,
        },
        "merged_before": per_split,
        "annotation_stats": ann_stats,
        "selected": {
            "images": len(selected),
            "bboxes": n_boxes,
            "with_fallen": n_fallen,
            "negatives": len(selected_negatives),
        },
        "applied": None,
    }

    if not args.apply:
        print("\n[4/4] dry-run のため何も書き込みません")
        print(f"  追加後の train 見込み: {per_split.get('train', 0)}"
              f" -> {per_split.get('train', 0) + len(selected) + len(selected_negatives)}")
        print("\n(--apply を付けるとダウンロードして merged/train に追加します)")
    else:
        eval_hashes = []
        if not args.skip_dedup:
            print("\n[4/4] val/test の perceptual hash を計算中 "
                  "(near-duplicate リーク防止) ...")
            eval_hashes = build_eval_hashes(merged_dir, args.dedup_hash_size)
            print(f"  ハッシュ済み: {len(eval_hashes)} 画像")
        else:
            print("\n[4/4] --skip-dedup: near-duplicate 検査を省略します")

        print("\n画像を取得して merged に追加中 ...")
        result, added_names = apply_expansion(
            selected + selected_negatives,
            merged_dir=merged_dir,
            source_out=args.source_out.resolve(),
            target_split=args.target_split,
            eval_hashes=eval_hashes,
            dedup_hamming=args.dedup_hamming,
            hash_size=args.dedup_hash_size,
        )
        print("\n--- 追加結果 ---")
        for k, v in result.items():
            print(f"  {k:24s}: {v}")

        list_name = {"train": "train.txt", "val": "valid.txt", "test": "test.txt"}.get(
            args.target_split, f"{args.target_split}.txt"
        )
        count, list_path = regenerate_split_list(
            merged_dir, args.target_split, list_name
        )
        print(f"  {list_name} 再生成    : {count} 画像 -> {list_path}")

        summary["applied"] = result
        summary["list_regenerated"] = {"path": str(list_path), "count": count}

        print("\nNOTE: 追加分の原本は " + str(args.source_out) + " に残ります。")
        print("      切り戻すには merged/images/" + args.target_split
              + " と merged/labels/" + args.target_split
              + f" の {EXT_PREFIX}* を削除し、リストを再生成してください。")
        print("NOTE: Colab へ渡す zip を作り直し、必ず検証してください:")
        print("      cd " + str(merged_dir))
        print("      zip -r fall_detection_dataset_v2.zip images/ labels/")
        print("      python3 -c \"import zipfile,sys; zipfile.ZipFile(sys.argv[1]); "
              "print('OK')\" fall_detection_dataset_v2.zip")

    if args.report is not None:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(summary, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        print(f"\nReport saved: {args.report}")


if __name__ == "__main__":
    main()
