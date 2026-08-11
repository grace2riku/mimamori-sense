"""
倒れ姿勢サブセット Recall 診断スクリプト (Issue #148 / 案G-①)

YOLO-Fastest 人物検出モデルの Recall が 49-52% で頭打ち (Issue #137 / Phase 3) に
なっている原因のうち、「倒れた姿勢 (横たわった人物) を系統的に見逃していないか」を
切り分けるための診断スクリプト。

darknet の `detector map` は全 GT をまとめた Recall しか出さないため、
GT (正解 bbox) を姿勢・サイズ・データソース別のサブグループに分け、
**グループごとの Recall / 見逃し数 (FN)** を出す。

判定基準 (サブセットの切り出し):
  - 姿勢    : bbox のアスペクト比 AR = w/h
              AR >= --fallen-ar (既定 1.2)  -> fallen   (横長 = 倒れ姿勢とみなす)
              AR <= --upright-ar (既定 0.8) -> upright  (縦長 = 立位/歩行)
              その間                        -> ambiguous
              ※ YOLO ラベルの w,h は画像サイズで正規化されているため、そのまま比を
                取ると画像のアスペクト比の分だけ歪む。既定では画像の実寸を読んで
                ピクセル基準の AR に補正する (--no-image-size で無効化)。
  - サイズ  : 正規化 bbox 面積 (w*h) で small / medium / large に区分
  - ソース  : ファイル名の接頭辞 (coco_ / rf_ / cocoext_ / hardneg_ ...)
              rf_ は Roboflow Fall Detection 由来 = 転倒データセット

評価条件は Issue #137 / #148 の KPI と揃える:
  - conf_thresh = 0.25 (--conf-threshold)
  - IoU 50%     = 0.5  (--iou-threshold)
  対応付けは「conf 降順の貪欲マッチング (1 検出 : 1 GT)」で行う。

使い方 (Colab 想定):
  # 1) まず darknet で評価対象 split (既定 val) を推論し JSON を出力する
  #    -thresh は --conf-threshold 以下にしておくこと (低い方の検出も渡す)
  #    cd /content/Yolo-Fastest
  #    ./darknet detector test data/person/obj.data \
  #        cfg/yolo-fastest-person-192-test.cfg \
  #        backup/yolo-fastest-person-192_best.weights \
  #        -dont_show -ext_output -thresh 0.25 \
  #        -out /content/val_predictions.json \
  #        < data/person/valid.txt
  #
  # 2) 本スクリプトでサブグループ別 Recall を算出
  python fallen_pose_recall_eval.py \
      --dataset /content/dataset \
      --split val \
      --predictions /content/val_predictions.json \
      --conf-threshold 0.25 \
      --iou-threshold 0.5 \
      --report /content/issue148_fallen_recall.json

使い方 (dry-run: GT の構成だけを確認する。推論 JSON 不要):
  python fallen_pose_recall_eval.py --dataset /content/dataset --split val --dry-run

Notes:
- darknet detector test の -out JSON 形式は hard_negative_mining.py と同じ:
  [{ "frame_id":..., "filename":"...", "objects":[{ "name","confidence",
    "relative_coordinates":{ "center_x","center_y","width","height" }}]}, ...]
  座標は元画像に対する相対値 (0-1)。GT (YOLO ラベル) も相対値なので、
  IoU はそのまま相対座標同士で計算できる。
- 本スクリプトはデータセットを一切変更しない (読み取りのみ)。
  `--list-out` を指定したときだけ、倒れ姿勢 GT を含む画像のリストを書き出す。
- AR による姿勢判定は近似である。正面から見た仰臥位など「横長にならない倒れ姿勢」は
  upright/ambiguous に落ちる。数値は「横長な人物」の Recall と解釈すること。
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path


IMAGE_EXTENSIONS = (".jpg", ".jpeg", ".png", ".bmp")

# 正規化 bbox 面積 (w*h) の区分。small は 192x192 入力では 1 辺 ~19px 相当以下。
AREA_BINS = (
    ("small", 0.0, 0.01),
    ("medium", 0.01, 0.10),
    ("large", 0.10, 1.01),
)


# ---------------------------------------------------------------------------
# IoU (hard_negative_mining.py と同じ定義)
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
# GT 読み込み・サブグループ分類
# ---------------------------------------------------------------------------

def image_path_for(stem: str, dataset_dir: Path, split: str):
    for ext in IMAGE_EXTENSIONS:
        p = dataset_dir / "images" / split / (stem + ext)
        if p.exists():
            return p
    return None


def image_size(path: Path):
    """(width, height) を返す。読めない場合は None。"""
    try:
        from PIL import Image  # 遅延 import (dry-run で PIL 不要なケースがある)
    except ImportError:
        return None
    try:
        with Image.open(path) as im:
            return im.size  # (w, h)
    except Exception:
        return None


def classify_pose(ar: float, fallen_ar: float, upright_ar: float) -> str:
    if ar >= fallen_ar:
        return "fallen"
    if ar <= upright_ar:
        return "upright"
    return "ambiguous"


def classify_area(area: float) -> str:
    for name, lo, hi in AREA_BINS:
        if lo <= area < hi:
            return name
    return AREA_BINS[-1][0]


def classify_source(stem: str, prefixes) -> str:
    for pre in prefixes:
        if pre and stem.startswith(pre):
            return pre.rstrip("_")
    return "other"


def load_ground_truth(
    dataset_dir: Path,
    split: str,
    fallen_ar: float,
    upright_ar: float,
    prefixes,
    use_image_size: bool,
):
    """labels/<split>/*.txt を読み、GT ごとに属性を付けて返す。

    返り値:
      gt_by_stem: {stem: [gt, ...]}  gt = dict(box, pose, area_bin, source, matched)
      stats: dict (走査統計)
    """
    lbl_dir = dataset_dir / "labels" / split
    if not lbl_dir.is_dir():
        raise FileNotFoundError(f"ラベルディレクトリが見つかりません: {lbl_dir}")

    gt_by_stem: dict[str, list] = {}
    stats = {
        "label_files": 0,
        "empty_label_files": 0,
        "gt_boxes": 0,
        "image_size_resolved": 0,
        "image_size_missing": 0,
        "malformed_lines": 0,
        "float_class_id_lines": 0,
    }

    for txt_path in sorted(lbl_dir.iterdir()):
        if txt_path.suffix != ".txt":
            continue
        stats["label_files"] += 1
        stem = txt_path.stem
        try:
            lines = txt_path.read_text(encoding="utf-8").splitlines()
        except Exception:
            stats["malformed_lines"] += 1
            continue

        # 画像実寸によるアスペクト比補正
        scale = 1.0
        if use_image_size:
            img_p = image_path_for(stem, dataset_dir, split)
            size = image_size(img_p) if img_p is not None else None
            if size and size[0] > 0 and size[1] > 0:
                # AR_px = (w_norm * W) / (h_norm * H) = (w_norm / h_norm) * (W / H)
                scale = size[0] / size[1]
                stats["image_size_resolved"] += 1
            else:
                stats["image_size_missing"] += 1

        boxes = []
        for raw in lines:
            parts = raw.strip().split()
            if not parts:
                continue
            if len(parts) != 5:
                stats["malformed_lines"] += 1
                continue
            try:
                # augment_offline.py:81 が生成する _aug ラベルはクラス ID を
                # "0.0" (float 表記) で書く。int() では ValueError になるため
                # float 経由で受ける。件数は float_class_id_lines で可視化する
                # (darknet の read_boxes は "%d %f %f %f %f" で読むため、
                #  この表記は学習側で誤読される。詳細は方針書 8 章 R8)。
                if "." in parts[0] or "e" in parts[0].lower():
                    stats["float_class_id_lines"] += 1
                _cls = int(float(parts[0]))
                cx, cy, w, h = (float(v) for v in parts[1:])
            except ValueError:
                stats["malformed_lines"] += 1
                continue
            if w <= 0 or h <= 0:
                stats["malformed_lines"] += 1
                continue
            ar = (w / h) * scale
            boxes.append({
                "box": (cx, cy, w, h),
                "ar": ar,
                "pose": classify_pose(ar, fallen_ar, upright_ar),
                "area_bin": classify_area(w * h),
                "source": classify_source(stem, prefixes),
                "matched": False,
                "best_iou": 0.0,
                "match_conf": 0.0,
            })
            stats["gt_boxes"] += 1

        if not boxes:
            stats["empty_label_files"] += 1
        gt_by_stem[stem] = boxes

    return gt_by_stem, stats


# ---------------------------------------------------------------------------
# 予測との貪欲マッチング
# ---------------------------------------------------------------------------

def match_predictions(
    predictions,
    gt_by_stem,
    conf_threshold: float,
    iou_threshold: float,
):
    """conf 降順の貪欲マッチングで TP/FP を決める。

    GT 側の dict を破壊的に更新 (matched / best_iou / match_conf)。
    返り値: stats
    """
    stats = {
        "frames_in_predictions": 0,
        "frames_matched_to_gt": 0,
        "frames_not_in_gt": 0,
        "detections_total": 0,
        "detections_over_conf": 0,
        "tp": 0,
        "fp": 0,
    }

    for entry in predictions:
        stats["frames_in_predictions"] += 1
        filename = entry.get("filename") or entry.get("frame") or ""
        stem = Path(filename).stem
        objects = entry.get("objects", []) or []
        stats["detections_total"] += len(objects)

        gts = gt_by_stem.get(stem)
        if gts is None:
            stats["frames_not_in_gt"] += 1
            # GT が無い画像の検出はすべて FP (負例画像など)
            for obj in objects:
                if float(obj.get("confidence", 0.0)) >= conf_threshold:
                    stats["detections_over_conf"] += 1
                    stats["fp"] += 1
            continue
        stats["frames_matched_to_gt"] += 1

        dets = []
        for obj in objects:
            conf = float(obj.get("confidence", 0.0))
            if conf < conf_threshold:
                continue
            rc = obj.get("relative_coordinates", {})
            dets.append((
                conf,
                (
                    float(rc.get("center_x", 0.0)),
                    float(rc.get("center_y", 0.0)),
                    float(rc.get("width", 0.0)),
                    float(rc.get("height", 0.0)),
                ),
            ))
        dets.sort(key=lambda d: d[0], reverse=True)
        stats["detections_over_conf"] += len(dets)

        for conf, det in dets:
            best_i = -1
            best_iou = 0.0
            for i, gt in enumerate(gts):
                if gt["matched"]:
                    continue
                v = iou_xywh(det, gt["box"])
                if v > best_iou:
                    best_iou = v
                    best_i = i
            if best_i >= 0 and best_iou >= iou_threshold:
                gts[best_i]["matched"] = True
                gts[best_i]["best_iou"] = best_iou
                gts[best_i]["match_conf"] = conf
                stats["tp"] += 1
            else:
                stats["fp"] += 1

    return stats


# ---------------------------------------------------------------------------
# 集計
# ---------------------------------------------------------------------------

def aggregate(gt_by_stem, key: str):
    """GT をグループ化して {group: {total, tp, fn, recall}} を返す。"""
    agg: dict[str, dict] = {}
    for boxes in gt_by_stem.values():
        for gt in boxes:
            g = agg.setdefault(gt[key], {"total": 0, "tp": 0, "fn": 0, "recall": 0.0})
            g["total"] += 1
            if gt["matched"]:
                g["tp"] += 1
            else:
                g["fn"] += 1
    for g in agg.values():
        g["recall"] = g["tp"] / g["total"] if g["total"] else 0.0
    return agg


def aggregate_cross(gt_by_stem, key_a: str, key_b: str):
    """2 軸のクロス集計 {"a|b": {...}}。"""
    agg: dict[str, dict] = {}
    for boxes in gt_by_stem.values():
        for gt in boxes:
            k = f'{gt[key_a]}|{gt[key_b]}'
            g = agg.setdefault(k, {"total": 0, "tp": 0, "fn": 0, "recall": 0.0})
            g["total"] += 1
            if gt["matched"]:
                g["tp"] += 1
            else:
                g["fn"] += 1
    for g in agg.values():
        g["recall"] = g["tp"] / g["total"] if g["total"] else 0.0
    return agg


def print_group_table(title: str, agg: dict, dry_run: bool):
    print(f"\n-- {title} --")
    if dry_run:
        print(f"  {'group':<20s} {'GT数':>8s} {'構成比':>8s}")
        total_all = sum(g["total"] for g in agg.values()) or 1
        for name in sorted(agg, key=lambda n: -agg[n]["total"]):
            g = agg[name]
            print(f"  {name:<20s} {g['total']:>8d} {g['total']/total_all*100:>7.1f}%")
        return
    print(f"  {'group':<20s} {'GT数':>8s} {'TP':>7s} {'FN':>7s} {'Recall':>8s}")
    for name in sorted(agg, key=lambda n: -agg[n]["total"]):
        g = agg[name]
        print(f"  {name:<20s} {g['total']:>8d} {g['tp']:>7d} {g['fn']:>7d} "
              f"{g['recall']*100:>7.1f}%")


# ---------------------------------------------------------------------------
# main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--dataset", type=Path, default=Path("/content/dataset"),
                        help="データセットルート (images/<split>, labels/<split> 構造)")
    parser.add_argument("--split", type=str, default="val",
                        help="診断対象の split (既定: val = KPI 評価と同じ)")
    parser.add_argument("--predictions", type=Path, default=None,
                        help="darknet detector test -out が出力した JSON "
                             "(--dry-run 時は不要)")
    parser.add_argument("--conf-threshold", type=float, default=0.25,
                        help="この信頼度以上の検出のみ使う (KPI 条件と同じ 0.25)")
    parser.add_argument("--iou-threshold", type=float, default=0.5,
                        help="GT と対応付ける IoU しきい値 (IoU50)")
    parser.add_argument("--fallen-ar", type=float, default=1.2,
                        help="bbox の w/h がこの値以上なら fallen (倒れ姿勢) とみなす")
    parser.add_argument("--upright-ar", type=float, default=0.8,
                        help="bbox の w/h がこの値以下なら upright (立位) とみなす")
    parser.add_argument("--source-prefixes", type=str,
                        default="coco_,rf_,cocoext_,hardneg_",
                        help="ソース分類に使うファイル名接頭辞 (カンマ区切り)")
    parser.add_argument("--no-image-size", action="store_true",
                        help="画像実寸によるアスペクト比補正を行わない "
                             "(正規化 w/h をそのまま使う。PIL 不要・高速)")
    parser.add_argument("--dry-run", action="store_true",
                        help="GT の構成だけを集計する (推論 JSON 不要)")
    parser.add_argument("--list-out", type=Path, default=None,
                        help="倒れ姿勢 GT を含む画像のパス一覧を書き出す先")
    parser.add_argument("--report", type=Path, default=None,
                        help="レポート JSON の出力先")
    args = parser.parse_args()

    dataset_dir: Path = args.dataset.resolve()
    if not (dataset_dir / "images").is_dir():
        print(f"ERROR: {dataset_dir}/images が見つかりません", file=sys.stderr)
        sys.exit(1)

    prefixes = [p.strip() for p in args.source_prefixes.split(",") if p.strip()]

    print(f"Dataset      : {dataset_dir}")
    print(f"Split        : {args.split}")
    print(f"Pose 判定    : fallen AR>={args.fallen_ar} / upright AR<={args.upright_ar}")
    print(f"AR 補正      : {'なし (正規化 w/h)' if args.no_image_size else 'あり (画像実寸)'}")
    if not args.dry_run:
        print(f"conf/IoU     : conf>={args.conf_threshold} / IoU>={args.iou_threshold}")

    print("\n[1/3] GT を走査中 ...")
    gt_by_stem, gt_stats = load_ground_truth(
        dataset_dir, args.split,
        fallen_ar=args.fallen_ar,
        upright_ar=args.upright_ar,
        prefixes=prefixes,
        use_image_size=not args.no_image_size,
    )
    print(f"  ラベルファイル : {gt_stats['label_files']}")
    print(f"  GT bbox        : {gt_stats['gt_boxes']}")
    print(f"  空ラベル       : {gt_stats['empty_label_files']}")
    if gt_stats["malformed_lines"]:
        print(f"  不正行 (無視)  : {gt_stats['malformed_lines']}")
    if gt_stats["float_class_id_lines"]:
        print(f"  クラスID が float 表記 : {gt_stats['float_class_id_lines']} 行")
        print("    WARNING: darknet の read_boxes は \"%d %f %f %f %f\" で読むため、")
        print("             \"0.0\" 表記は学習時に誤読される恐れがある (方針書 8 章 R8)。")
        print("             学習前に fix_float_class_ids.py で正規化すること。")
    if not args.no_image_size:
        print(f"  実寸取得 成功/失敗 : {gt_stats['image_size_resolved']}"
              f"/{gt_stats['image_size_missing']}")

    match_stats = None
    if not args.dry_run:
        if args.predictions is None:
            print("ERROR: --predictions が未指定です "
                  "(GT 構成のみ見る場合は --dry-run を付けてください)", file=sys.stderr)
            sys.exit(1)
        if not args.predictions.exists():
            print(f"ERROR: predictions JSON が見つかりません: {args.predictions}",
                  file=sys.stderr)
            print("  先に darknet detector test -out で予測を出力してください",
                  file=sys.stderr)
            sys.exit(1)
        print("\n[2/3] 予測を読み込みマッチング中 ...")
        try:
            predictions = json.loads(args.predictions.read_text(encoding="utf-8"))
        except Exception as e:
            print(f"ERROR: predictions JSON のパースに失敗: {e}", file=sys.stderr)
            sys.exit(1)
        if not isinstance(predictions, list):
            print("ERROR: predictions JSON は配列である必要があります", file=sys.stderr)
            sys.exit(1)
        match_stats = match_predictions(
            predictions, gt_by_stem,
            conf_threshold=args.conf_threshold,
            iou_threshold=args.iou_threshold,
        )
        if match_stats["frames_not_in_gt"]:
            print(f"  WARNING: GT に対応が無い画像が {match_stats['frames_not_in_gt']} 件 "
                  f"(split の指定違い、または負例画像)")
    else:
        print("\n[2/3] dry-run のため予測とのマッチングはスキップ")

    print("\n[3/3] 集計")
    print("=" * 68)
    print(f"倒れ姿勢サブセット Recall 診断 (Issue #148 / split={args.split})")
    print("=" * 68)

    if match_stats is not None:
        tp = match_stats["tp"]
        fp = match_stats["fp"]
        total_gt = gt_stats["gt_boxes"]
        fn = total_gt - tp
        prec = tp / (tp + fp) if (tp + fp) else 0.0
        rec = tp / total_gt if total_gt else 0.0
        print("\n-- 全体 (darknet detector map との突き合わせ用) --")
        print(f"  GT (unique_truth 相当) : {total_gt}")
        print(f"  TP / FP / FN           : {tp} / {fp} / {fn}")
        print(f"  Precision              : {prec*100:.1f}%")
        print(f"  Recall                 : {rec*100:.1f}%")
        print("  NOTE: darknet detector map の Precision/Recall とは実装差 "
              "(貪欲マッチングの順序等) で数 pt ずれることがある。"
              "本スクリプトの用途はグループ間の相対比較。")

    pose_agg = aggregate(gt_by_stem, "pose")
    area_agg = aggregate(gt_by_stem, "area_bin")
    src_agg = aggregate(gt_by_stem, "source")
    cross_agg = aggregate_cross(gt_by_stem, "pose", "area_bin")

    print_group_table("姿勢別 (pose)", pose_agg, args.dry_run)
    print_group_table("bbox サイズ別 (area)", area_agg, args.dry_run)
    print_group_table("データソース別 (source)", src_agg, args.dry_run)
    print_group_table("姿勢 x サイズ (cross)", cross_agg, args.dry_run)

    if not args.dry_run:
        f = pose_agg.get("fallen", {}).get("recall", 0.0)
        u = pose_agg.get("upright", {}).get("recall", 0.0)
        print("\n-- 判定 --")
        print(f"  fallen Recall  : {f*100:.1f}%")
        print(f"  upright Recall : {u*100:.1f}%")
        if pose_agg.get("fallen", {}).get("total", 0) < 100:
            print("  NOTE: fallen の GT 数が 100 未満のため統計的な信頼性は低い。"
                  "案G のデータ拡充で母数を増やしてから再評価すること。")
        elif f + 0.05 <= u:
            print("  -> 倒れ姿勢の Recall が立位より 5pt 以上低い。"
                  "**倒れ姿勢の系統的な見逃しあり**と判断できる (案G-① 該当)。")
        elif u + 0.05 <= f:
            print("  -> 倒れ姿勢の方が高い。Recall 律速は倒れ姿勢ではない。"
                  "サイズ別 (small) の Recall を確認すること。")
        else:
            print("  -> 姿勢による有意差なし。Recall 律速は姿勢ではなく "
                  "解像度/モデル容量/データ量側 (#137 6.1.3 の結論と整合)。")

    print("\n" + "=" * 68)

    # --- 倒れ姿勢を含む画像リストの書き出し ---
    if args.list_out is not None:
        fallen_images = []
        for stem, boxes in sorted(gt_by_stem.items()):
            if any(b["pose"] == "fallen" for b in boxes):
                p = image_path_for(stem, dataset_dir, args.split)
                if p is not None:
                    fallen_images.append(str(p))
        args.list_out.parent.mkdir(parents=True, exist_ok=True)
        args.list_out.write_text(
            "\n".join(fallen_images) + ("\n" if fallen_images else ""),
            encoding="utf-8",
        )
        print(f"倒れ姿勢を含む画像リストを出力: {args.list_out} ({len(fallen_images)} 件)")
        print("  NOTE: このリストを darknet detector map の valid に使うと、"
              "同じ画像に写る立位の GT も母数に入る。"
              "姿勢別 Recall は本スクリプトの出力を使うこと。")

    # --- レポート JSON ---
    if args.report is not None:
        payload = {
            "issue": "#148",
            "dataset": str(dataset_dir),
            "split": args.split,
            "dry_run": args.dry_run,
            "params": {
                "conf_threshold": args.conf_threshold,
                "iou_threshold": args.iou_threshold,
                "fallen_ar": args.fallen_ar,
                "upright_ar": args.upright_ar,
                "use_image_size": not args.no_image_size,
                "source_prefixes": prefixes,
            },
            "gt_stats": gt_stats,
            "match_stats": match_stats,
            "by_pose": pose_agg,
            "by_area": area_agg,
            "by_source": src_agg,
            "by_pose_area": cross_agg,
        }
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False), encoding="utf-8"
        )
        print(f"Report saved: {args.report}")

    if args.dry_run:
        print("\n(dry-run モード: --predictions を渡すと Recall を算出します)")


if __name__ == "__main__":
    main()
