"""実行結果ノートブック（*_output.ipynb）に失敗が無いか検査する。

実行場所: ローカル（WSL）
使い方  : python3 dataset/scripts/colab_cli/check_notebook_errors.py <notebook>_output.ipynb

検出するもの:
  1. セルの例外（`output_type: "error"`）
     `colab exec -f <nb>.ipynb` は**セルがエラーになっても後続セルを実行し続ける**ため、
     CLI の標準出力だけでは成功に見える。
  2. 出力テキスト中の失敗マーカー（`ERROR:` / `SKIP:` / `WARNING:`）
     学習ノートブックには、前提ファイルが無いときに例外を投げず
     `print('ERROR: ...')` や `print('SKIP: ...')` を出して処理を飛ばすセルがある
     （cell[10] データセットクリーニング、cell[12] hard negative mining）。
     例外にならないため 1. では検出できず、必須の前処理を飛ばしたまま学習が進みうる。

どちらか一方でも見つかれば終了コード 1 を返すので、シェルスクリプトからも判定できる。
"""
import json
import sys

# 出力テキスト中で失敗・スキップを示すマーカー
MARKERS = ("ERROR:", "SKIP:", "WARNING:")

if len(sys.argv) != 2:
    print(__doc__)
    sys.exit(2)

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    nb = json.load(f)


def stream_text(cell):
    """セルの stream / execute_result 出力を 1 本のテキストにまとめる。"""
    parts = []
    for o in cell.get("outputs", []):
        if o.get("output_type") == "stream":
            parts.append("".join(o.get("text", [])))
        elif o.get("output_type") in ("execute_result", "display_data"):
            parts.append("".join(o.get("data", {}).get("text/plain", [])))
    return "".join(parts)


errors = 0
flagged = 0
for i, cell in enumerate(nb.get("cells", [])):
    if cell.get("cell_type") != "code":
        continue
    head = "".join(cell.get("source", []))[:50].replace("\n", " ")
    kinds = [o.get("output_type") for o in cell.get("outputs", [])]
    errs = [o for o in cell.get("outputs", []) if o.get("output_type") == "error"]

    hits = [
        line.strip()
        for line in stream_text(cell).splitlines()
        if any(m in line for m in MARKERS)
    ]

    mark = "★ERROR" if errs else ("▲SKIP " if hits else "      ")
    print(f"{mark} [{i}] {head!r:55s} outputs={kinds}")
    for e in errs:
        errors += 1
        print(f"        {e.get('ename')}: {str(e.get('evalue'))[:120]}")
    for line in hits:
        flagged += 1
        print(f"        {line[:120]}")

print()
if errors or flagged:
    if errors:
        print(f"例外が発生したセル: {errors} 件")
    if flagged:
        print(f"失敗・スキップを示す出力: {flagged} 件")
        print("  前提ファイルの不足で処理が飛ばされていないか確認してください。")
    sys.exit(1)
print("エラーなし")
