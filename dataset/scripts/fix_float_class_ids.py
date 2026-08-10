"""YOLO ラベルのクラス ID が float 表記 ("0.0") になっている行を整数 ("0") に正規化する。

背景 (Issue #148):
  `augment_offline.py:81` は albumentations が返すクラス ID をそのまま f-string で
  書き出す。albumentations は label_fields の値を float 化して返すため、生成される
  `*_aug<N>.txt` のクラス ID が "0.0" になる。

  YOLO のラベル仕様ではクラス ID は整数である。darknet の read_boxes は
  `fscanf(file, "%d %f %f %f %f", ...)` 形式で読むとされており、その場合
  "%d" が "0" だけを消費して ".0" が次の "%f" に流れ込み、以降のフィールドが
  1 つずつずれる (x=.0, y=<本来のx>, w=<本来のy>, h=<本来のw>)。
  ただし本リポジトリに darknet のソースは無く、書式文字列そのものは未確認である
  (方針書 f003_03l 8 章 R8)。

  いずれにせよ仕様から外れた表記であり、同じデータセット内の非 aug ラベルは "0" を
  使っているため、正規化しておくのが安全である。

使い方:
  # 影響範囲の確認 (既定: 何も書き換えない)
  python dataset/scripts/fix_float_class_ids.py

  # 実適用
  python dataset/scripts/fix_float_class_ids.py --apply --report fix_report.json

注意:
  --apply はラベルファイルを上書きする。bbox 座標には触れず、行頭のクラス ID
  トークンのみを置き換える。座標の桁や行順は保持される。
"""
import argparse
import json
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
DEFAULT_DATASET = SCRIPT_DIR.parent / "merged"


def normalize_line(raw: str):
    """1 行を正規化する。(新しい行, 変更したか, 不正か) を返す。"""
    stripped = raw.strip()
    if not stripped:
        return raw, False, False
    parts = stripped.split()
    if len(parts) != 5:
        return raw, False, True
    token = parts[0]
    try:
        value = float(token)
    except ValueError:
        return raw, False, True
    if value != int(value):
        # 0.5 のような非整数クラス ID は自動修正しない (想定外の壊れ方)
        return raw, False, True
    canonical = str(int(value))
    if token == canonical:
        return raw, False, False
    parts[0] = canonical
    return " ".join(parts) + "\n", True, False


def process_split(lbl_dir: Path, apply: bool):
    stats = {
        "label_files": 0,
        "files_changed": 0,
        "lines_total": 0,
        "lines_changed": 0,
        "lines_malformed": 0,
    }
    changed_files = []
    if not lbl_dir.is_dir():
        return stats, changed_files

    for txt_path in sorted(lbl_dir.iterdir()):
        if txt_path.suffix != ".txt":
            continue
        stats["label_files"] += 1
        try:
            lines = txt_path.read_text(encoding="utf-8").splitlines(keepends=True)
        except Exception as e:
            print(f"  WARNING: 読み取り失敗 {txt_path.name}: {e}", file=sys.stderr)
            continue

        out_lines = []
        file_changed = False
        for raw in lines:
            stats["lines_total"] += 1
            new_raw, changed, malformed = normalize_line(raw)
            if malformed:
                stats["lines_malformed"] += 1
            if changed:
                stats["lines_changed"] += 1
                file_changed = True
            out_lines.append(new_raw)

        if file_changed:
            stats["files_changed"] += 1
            changed_files.append(txt_path.name)
            if apply:
                txt_path.write_text("".join(out_lines), encoding="utf-8")

    return stats, changed_files


def main():
    parser = argparse.ArgumentParser(
        description="YOLO ラベルの float 表記クラス ID を整数に正規化する"
    )
    parser.add_argument("--dataset", type=Path, default=DEFAULT_DATASET,
                        help=f"データセットルート (既定: {DEFAULT_DATASET})")
    parser.add_argument("--splits", default="train,val,test",
                        help="対象 split をカンマ区切りで指定 (既定: train,val,test)")
    parser.add_argument("--apply", action="store_true",
                        help="実際にラベルファイルを書き換える (未指定時は dry-run)")
    parser.add_argument("--report", type=Path, default=None,
                        help="結果を JSON で保存するパス")
    args = parser.parse_args()

    dataset = args.dataset.resolve()
    splits = [s.strip() for s in args.splits.split(",") if s.strip()]

    print(f"dataset : {dataset}")
    print(f"splits  : {', '.join(splits)}")
    print(f"mode    : {'APPLY (書き換える)' if args.apply else 'dry-run (書き換えない)'}")
    print()

    report = {"issue": "#148", "dataset": str(dataset), "apply": args.apply,
              "splits": {}}
    total_changed = 0
    for split in splits:
        lbl_dir = dataset / "labels" / split
        stats, changed_files = process_split(lbl_dir, args.apply)
        report["splits"][split] = stats
        total_changed += stats["lines_changed"]
        print(f"[{split}]")
        print(f"  ラベルファイル : {stats['label_files']}")
        print(f"  要修正ファイル : {stats['files_changed']}")
        print(f"  行 合計/要修正 : {stats['lines_total']} / {stats['lines_changed']}")
        if stats["lines_malformed"]:
            print(f"  不正行 (未修正): {stats['lines_malformed']}")
        if changed_files[:3]:
            print(f"  例: {', '.join(changed_files[:3])}")
        print()

    if total_changed == 0:
        print("すべてのクラス ID は整数表記です。修正は不要です。")
    elif args.apply:
        print(f"完了: {total_changed} 行を修正しました。")
        print("NOTE: アンカー値には影響しないが、学習データが変わるため")
        print("      Colab へ渡す zip を作り直すこと。")
    else:
        print(f"dry-run: {total_changed} 行が修正対象です。--apply で実行してください。")

    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(
            json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8"
        )
        print(f"\nReport saved: {args.report}")


if __name__ == "__main__":
    main()
