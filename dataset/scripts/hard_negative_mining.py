"""
Hard Negative Mining スクリプト (Issue #137 / Phase 3A)

YOLO-Fastest 人物検出モデルの False Positive (FP) を削減するため、
Phase 2 学習済み weights で train 画像を推論し、
「正解 bbox と重ならない高スコア検出 (= FP)」を多く出す画像を
hard negative として抽出し、空ラベル (.txt) 付きで train に追加する。

狙い:
- Phase 2 結果は FP=1994 と多く Precision=49%。背景 (家具/影/マネキン等) を
  人物と誤検出している。これらを negative サンプルとして再学習に含めることで
  FP を減らし Precision を引き上げる。

処理フロー:
  1. Phase 2 best weights + cfg で train 画像を darknet 推論
     (darknet detector test の出力 JSON を利用)
  2. 各検出を正解ラベルと IoU 照合し、
     IoU < --fp-iou-threshold かつ conf >= --fp-conf-threshold を FP とみなす
  3. FP を 1 つ以上含む画像を hard negative 候補とする
  4. 候補を <dataset>/hard_negatives/ にコピーし、空ラベル .txt を生成
  5. --apply 時のみ images/train, labels/train に追記し train.txt を再生成

本スクリプトは Phase 2 の dataset_cleaning.py と同じ作法に従う:
- デフォルト dry-run (統計 + 候補リストのみ)
- --apply 指定時のみ train に追加 (原本は hard_negatives/ に残るので切り戻し可能)
- --max-add-ratio で追加上限を制限 (データ偏り防止)
- レポートを JSON 出力

使い方 (Colab 想定, darknet 推論結果を入力):
  # 1) まず darknet で train 画像を推論し JSON を出力
  #    cd /content/Yolo-Fastest
  #    ./darknet detector test data/person/obj.data \
  #        cfg/yolo-fastest-person-192-test.cfg \
  #        backup/yolo-fastest-person-192_best.weights \
  #        -dont_show -ext_output -out /content/train_predictions.json \
  #        < data/person/train.txt
  #
  # 2) 本スクリプトで FP を抽出して hard negative を作成
  python hard_negative_mining.py \
      --dataset /content/dataset \
      --predictions /content/train_predictions.json \
      --fp-conf-threshold 0.3 \
      --fp-iou-threshold 0.1 \
      --max-add-ratio 0.15 \
      --report /content/phase3_hardneg_report.json \
      --apply

使い方 (dry-run):
  python hard_negative_mining.py \
      --dataset /content/dataset \
      --predictions /content/train_predictions.json

Notes:
- darknet detector test の -out JSON は
  [{ "frame_id":..., "filename":"...", "objects":[{ "name","confidence",
    "relative_coordinates":{ "center_x","center_y","width","height" }}]}, ...]
  形式 (相対座標 0-1)。本スクリプトはこの形式を前提とする。
- 追加された hard negative の原本は <dataset>/hard_negatives/ に残る。
  切り戻すには images/train, labels/train に追記した分を削除し
  train.txt を再生成すればよい (退避先がそのまま原本なので物理削除は不要)。
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path


IMAGE_EXTENSIONS = {".jpg", ".jpeg", ".png", ".bmp"}


# ---------------------------------------------------------------------------
# IoU
# ---------------------------------------------------------------------------

def iou_xywh(a, b) -> float:
    """中心 (cx,cy,w,h) 相対座標 2 個の IoU。"""
    ax1, ay1 = a[0] - a[2] / 2, a[1] - a[3] / 2
    ax2, ay2 = a[0] + a[2] / 2, a[1] + a[3] / 2
    bx1, by1 = b[0] - b[2] / 2, b[1] - b[3] / 2
    bx2, by2 = b[0] + b[2] / 2, b[1] + b[3] / 2

    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    if inter <= 0:
        return 0.0
    area_a = max(0.0, ax2 - ax1) * max(0.0, ay2 - ay1)
    area_b = max(0.0, bx2 - bx1) * max(0.0, by2 - by1)
    union = area_a + area_b - inter
    if union <= 0:
        return 0.0
    return inter / union


# ---------------------------------------------------------------------------
# ラベル読み込み
# ---------------------------------------------------------------------------

def load_gt_boxes(label_path: Path):
    """YOLO ラベル .txt -> [(cx,cy,w,h), ...] (相対座標)。"""
    boxes = []
    if not label_path.exists():
        return boxes
    try:
        for raw in label_path.read_text().splitlines():
            parts = raw.strip().split()
            if len(parts) != 5:
                continue
            try:
                _cls = int(parts[0])
                cx, cy, w, h = (float(v) for v in parts[1:])
            except ValueError:
                continue
            boxes.append((cx, cy, w, h))
    except Exception:
        pass
    return boxes


def label_path_for(image_path: Path, dataset_dir: Path) -> Path:
    split = image_path.parent.name
    return dataset_dir / "labels" / split / (image_path.stem + ".txt")


# ---------------------------------------------------------------------------
# FP 抽出
# ---------------------------------------------------------------------------

def find_false_positives(
    predictions,
    dataset_dir: Path,
    fp_conf_threshold: float,
    fp_iou_threshold: float,
):
    """darknet predictions JSON から FP を含む画像を抽出する。

    返り値:
      candidates: list of dict {filename, fp_count, max_fp_conf}
      stats: dict
    """
    candidates = []
    stats = {
        "frames_total": 0,
        "frames_with_detections": 0,
        "frames_with_fp": 0,
        "total_detections": 0,
        "total_fp": 0,
    }

    for entry in predictions:
        stats["frames_total"] += 1
        filename = entry.get("filename") or entry.get("frame") or ""
        objects = entry.get("objects", []) or []
        if objects:
            stats["frames_with_detections"] += 1
        stats["total_detections"] += len(objects)

        img_path = Path(filename)
        gt_boxes = load_gt_boxes(label_path_for(img_path, dataset_dir))

        fp_count = 0
        max_fp_conf = 0.0
        for obj in objects:
            conf = float(obj.get("confidence", 0.0))
            if conf < fp_conf_threshold:
                continue
            rc = obj.get("relative_coordinates", {})
            det = (
                float(rc.get("center_x", 0.0)),
                float(rc.get("center_y", 0.0)),
                float(rc.get("width", 0.0)),
                float(rc.get("height", 0.0)),
            )
            # 正解 bbox との最大 IoU
            best_iou = 0.0
            for gt in gt_boxes:
                best_iou = max(best_iou, iou_xywh(det, gt))
            if best_iou < fp_iou_threshold:
                fp_count += 1
                max_fp_conf = max(max_fp_conf, conf)

        if fp_count > 0:
            stats["frames_with_fp"] += 1
            stats["total_fp"] += fp_count
            candidates.append({
                "filename": filename,
                "fp_count": fp_count,
                "max_fp_conf": max_fp_conf,
            })

    return candidates, stats


# ---------------------------------------------------------------------------
# 追加適用
# ---------------------------------------------------------------------------

def apply_hard_negatives(
    candidates,
    dataset_dir: Path,
    max_add: int,
):
    """hard negative 候補を images/train, labels/train に追加する。

    各候補画像を <dataset>/hard_negatives/ にコピー (リネームして衝突回避)、
    images/train へコピー、空ラベル labels/train/<name>.txt を生成する。
    """
    hn_dir = dataset_dir / "hard_negatives"
    img_train = dataset_dir / "images" / "train"
    lbl_train = dataset_dir / "labels" / "train"
    hn_dir.mkdir(exist_ok=True)
    img_train.mkdir(parents=True, exist_ok=True)
    lbl_train.mkdir(parents=True, exist_ok=True)

    # fp_count の多い順に優先 (FP を最も出す画像を優先追加)
    ordered = sorted(candidates, key=lambda c: c["fp_count"], reverse=True)

    added = []
    for c in ordered[:max_add]:
        src = Path(c["filename"])
        if not src.exists():
            continue
        # 衝突回避のため接頭辞を付与
        new_stem = f"hardneg_{src.stem}"
        ext = src.suffix.lower() if src.suffix.lower() in IMAGE_EXTENSIONS else ".jpg"
        # 1) 原本を hard_negatives/ に保存 (切り戻し用)
        hn_copy = hn_dir / (new_stem + ext)
        if not hn_copy.exists():
            shutil.copy2(str(src), str(hn_copy))
        # 2) train に配置
        dst_img = img_train / (new_stem + ext)
        if not dst_img.exists():
            shutil.copy2(str(src), str(dst_img))
        # 3) 空ラベル (人物なし) を生成
        #    darknet (Yolo-Fastest fork) は画像と同じ images/train/ から .txt を
        #    読むため、labels/train/ と images/train/ の両方に空ラベルを置く。
        #    (labels/train 側は本データセットのラベル原本格納先との整合用)
        dst_lbl = lbl_train / (new_stem + ".txt")
        dst_lbl.write_text("")  # 空 = negative sample
        dst_lbl_next = img_train / (new_stem + ".txt")
        dst_lbl_next.write_text("")  # darknet が実際に参照する位置
        added.append(str(dst_img))

    return added


def regenerate_train_txt(dataset_dir: Path, data_dir: Path):
    """images/train を走査して train.txt を再生成する。"""
    import glob
    img_dir = dataset_dir / "images" / "train"
    images = sorted(
        glob.glob(str(img_dir / "*.jpg"))
        + glob.glob(str(img_dir / "*.jpeg"))
        + glob.glob(str(img_dir / "*.png"))
    )
    list_path = data_dir / "train.txt"
    list_path.parent.mkdir(parents=True, exist_ok=True)
    with open(list_path, "w") as f:
        for p in images:
            f.write(p + "\n")
    return len(images), list_path


# ---------------------------------------------------------------------------
# レポート
# ---------------------------------------------------------------------------

def print_report(stats, candidates, max_add, max_preview=10):
    print("\n" + "=" * 60)
    print("Phase 3A Hard Negative Mining Report")
    print("=" * 60)
    print(f"  frames scanned          : {stats['frames_total']}")
    print(f"  frames with detections  : {stats['frames_with_detections']}")
    print(f"  total detections        : {stats['total_detections']}")
    print(f"  frames with FP          : {stats['frames_with_fp']}")
    print(f"  total FP detections     : {stats['total_fp']}")
    print(f"  hard negative candidates: {len(candidates)}")
    print(f"  will add (cap)          : {min(len(candidates), max_add)}")
    print("  -- top FP frames preview --")
    ordered = sorted(candidates, key=lambda c: c["fp_count"], reverse=True)
    for c in ordered[:max_preview]:
        print(f"     {Path(c['filename']).name}: "
              f"fp={c['fp_count']} max_conf={c['max_fp_conf']:.2f}")
    print("=" * 60)


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dataset", type=Path, default=Path("/content/dataset"),
                        help="データセットルート (images/train, labels/train 構造)")
    parser.add_argument("--predictions", type=Path, required=True,
                        help="darknet detector test -out が出力した JSON")
    parser.add_argument("--data-dir", type=Path,
                        default=Path("/content/Yolo-Fastest/data/person"),
                        help="train.txt を再生成するディレクトリ")
    parser.add_argument("--fp-conf-threshold", type=float, default=0.3,
                        help="この信頼度以上の検出のみ FP 判定対象とする")
    parser.add_argument("--fp-iou-threshold", type=float, default=0.1,
                        help="正解 bbox との最大 IoU がこの値未満なら FP とみなす")
    parser.add_argument("--max-add-ratio", type=float, default=0.15,
                        help="元 train 枚数に対する hard negative 追加上限比率")
    parser.add_argument("--apply", action="store_true",
                        help="実際に train へ hard negative を追加する")
    parser.add_argument("--report", type=Path, default=None,
                        help="レポート JSON の出力先")
    args = parser.parse_args()

    dataset_dir: Path = args.dataset.resolve()
    if not (dataset_dir / "images").is_dir():
        print(f"ERROR: {dataset_dir}/images が見つかりません", file=sys.stderr)
        sys.exit(1)
    if not args.predictions.exists():
        print(f"ERROR: predictions JSON が見つかりません: {args.predictions}",
              file=sys.stderr)
        print("  先に darknet detector test -out で予測を出力してください",
              file=sys.stderr)
        sys.exit(1)

    print(f"Dataset    : {dataset_dir}")
    print(f"Predictions: {args.predictions}")
    print(f"FP conf    : >= {args.fp_conf_threshold}")
    print(f"FP IoU     : <  {args.fp_iou_threshold}")

    try:
        predictions = json.loads(args.predictions.read_text())
    except Exception as e:
        print(f"ERROR: predictions JSON のパースに失敗: {e}", file=sys.stderr)
        sys.exit(1)
    if not isinstance(predictions, list):
        print("ERROR: predictions JSON は配列である必要があります", file=sys.stderr)
        sys.exit(1)

    # 元 train 枚数 (追加上限の基準)
    import glob
    train_img_dir = dataset_dir / "images" / "train"
    base_train_count = len(
        glob.glob(str(train_img_dir / "*.jpg"))
        + glob.glob(str(train_img_dir / "*.jpeg"))
        + glob.glob(str(train_img_dir / "*.png"))
    )
    max_add = int(base_train_count * args.max_add_ratio)
    print(f"Base train : {base_train_count} imgs -> add cap {max_add} "
          f"({args.max_add_ratio:.0%})")

    candidates, stats = find_false_positives(
        predictions, dataset_dir,
        fp_conf_threshold=args.fp_conf_threshold,
        fp_iou_threshold=args.fp_iou_threshold,
    )

    print_report(stats, candidates, max_add)

    if args.report is not None:
        payload = {
            "dataset": str(dataset_dir),
            "predictions": str(args.predictions),
            "fp_conf_threshold": args.fp_conf_threshold,
            "fp_iou_threshold": args.fp_iou_threshold,
            "max_add_ratio": args.max_add_ratio,
            "base_train_count": base_train_count,
            "max_add": max_add,
            "stats": stats,
            "candidate_count": len(candidates),
            "candidates_preview": sorted(
                candidates, key=lambda c: c["fp_count"], reverse=True
            )[:50],
        }
        args.report.write_text(json.dumps(payload, indent=2))
        print(f"\nReport saved: {args.report}")

    if args.apply:
        if not candidates:
            print("\n[APPLY] hard negative 候補が 0 件のため追加なし")
            return
        print(f"\n[APPLY] hard negative を train に追加 (最大 {max_add} 枚) ...")
        added = apply_hard_negatives(candidates, dataset_dir, max_add)
        count, list_path = regenerate_train_txt(dataset_dir, args.data_dir.resolve())
        print(f"  追加枚数         : {len(added)}")
        print(f"  原本退避先       : {dataset_dir / 'hard_negatives'}")
        print(f"  train.txt 再生成 : {count} 枚 -> {list_path}")
        print("\nNOTE: 追加分の原本は hard_negatives/ に残ります。")
        print("      切り戻すには images/train, labels/train の hardneg_* を削除し")
        print("      train.txt を再生成してください。")
    else:
        print("\n(dry-run モード: --apply を指定すると train に追加します)")


if __name__ == "__main__":
    main()
