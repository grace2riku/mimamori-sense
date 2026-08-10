"""実行結果ノートブック（*_output.ipynb）に失敗が無いか検査する。

実行場所: ローカル（WSL）
使い方  : python3 dataset/scripts/colab_cli/check_notebook_errors.py <notebook>_output.ipynb

検出するもの:
  1. セルの例外（`output_type: "error"`）
     `colab exec -f <nb>.ipynb` は**セルがエラーになっても後続セルを実行し続ける**ため、
     CLI の標準出力だけでは成功に見える。
  2. 出力テキスト中の失敗マーカー（`ERROR:` / `SKIP:` / `WARNING:` / `Traceback`）
     学習ノートブックには、前提ファイルが無いときに例外を投げず
     `print('ERROR: ...')` や `print('SKIP: ...')` を出して処理を飛ばすセルがある
     （cell[10] データセットクリーニング、cell[12] hard negative mining）。
     例外にならないため 1. では検出できず、必須の前処理を飛ばしたまま学習が進みうる。
     同じセルは前処理スクリプトを `subprocess.run(..., check=False)` で呼ぶため、
     スクリプトが異常終了しても無視される。子プロセスの出力はセルに取り込まれるので、
     クラッシュした場合は `Traceback` で捕捉できる。

  3. 途中で打ち切られた形跡
     タイムアウトやカーネル中断で `colab exec` が返ると、未実行のセルは outputs が
     空のまま残る。`colab exec -f` は**実行済みのセルでも execution_count を null の
     ままにする**（実測で確認）ため、実行の有無は outputs で判断する。
     最後のコードセルに出力が無ければ最後まで走っていない可能性が高いのでエラーとする。

いずれかが見つかれば終了コード 1 を返すので、シェルスクリプトからも判定できる。

★検出できない範囲: `subprocess.run(..., check=False)` の終了コードは捨てられるため、
  前処理スクリプトが Traceback を出さずに異常終了した場合（引数エラーで usage を出して
  終了する等）は本スクリプトでは検出できない。実行後に Drive 上のレポート JSON
  （phase2_cleaning_report.json / phase3_hardneg_report.json）が更新されているかを
  必ず確認すること。手順はガイド 6.10 節「実行後の確認」を参照。
"""
import json
import sys

# 出力テキスト中で失敗・スキップを示すマーカー
MARKERS = ("ERROR:", "SKIP:", "WARNING:", "Traceback (most recent call last)")

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


code_cells = [
    (i, c) for i, c in enumerate(nb.get("cells", [])) if c.get("cell_type") == "code"
]
# `colab exec -f` は実行済みのセルでも execution_count を null のままにする（実測で確認済み）。
# そのため「実行されたか」の判断は outputs の有無で行う。
no_output = [i for i, c in code_cells if not c.get("outputs")]

errors = 0
flagged = 0
for i, cell in code_cells:
    head = "".join(cell.get("source", []))[:50].replace("\n", " ")
    kinds = [o.get("output_type") for o in cell.get("outputs", [])]
    errs = [o for o in cell.get("outputs", []) if o.get("output_type") == "error"]

    hits = [
        line.strip()
        for line in stream_text(cell).splitlines()
        if any(m in line for m in MARKERS)
    ]

    if errs:
        mark = "★ERROR"
    elif hits:
        mark = "▲SKIP "
    elif i in no_output:
        mark = "◇未実行?"
    else:
        mark = "      "

    print(f"{mark} [{i}] {head!r:55s} outputs={kinds}")
    for e in errs:
        errors += 1
        print(f"        {e.get('ename')}: {str(e.get('evalue'))[:120]}")
    for line in hits:
        flagged += 1
        print(f"        {line[:120]}")

# --- 途中で打ち切られていないかの判定 ---
# タイムアウトやカーネル中断で `colab exec` が返ると、未実行のセルは
# outputs が空のまま残る。最後のコードセルに出力が無ければ、
# ノートブックが最後まで走っていない可能性が高い。
truncated = False
if code_cells:
    last_index = code_cells[-1][0]
    if not code_cells[0][1].get("outputs") and len(no_output) == len(code_cells):
        print()
        print("★ERROR: すべてのコードセルに出力がありません（ノートブックが実行されていません）")
        truncated = True
    elif last_index in no_output:
        print()
        print("★ERROR: 最後のコードセルに出力がありません")
        print("  タイムアウトやカーネル中断で途中終了した可能性があります。")
        print(f"  出力の無いセル: {no_output}")
        truncated = True
    elif no_output:
        print()
        print(f"NOTE: 出力の無いコードセルがあります: {no_output}")
        print("  出力を出さないセルであれば問題ありませんが、意図した実行結果か確認してください。")

print()
if errors or flagged or truncated:
    if errors:
        print(f"例外が発生したセル: {errors} 件")
    if flagged:
        print(f"失敗・スキップを示す出力: {flagged} 件")
        print("  前提ファイルの不足や前処理スクリプトの失敗で、必要な処理が")
        print("  飛ばされていないか確認してください。")
    if truncated:
        print("ノートブックが最後まで実行されていない可能性があります。")
    sys.exit(1)
print("エラーなし")
