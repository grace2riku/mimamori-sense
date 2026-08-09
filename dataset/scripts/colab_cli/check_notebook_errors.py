"""実行結果ノートブック（*_output.ipynb）にセルエラーが無いか検査する。

実行場所: ローカル（WSL）
使い方  : python3 check_notebook_errors.py <notebook>_output.ipynb

`colab exec -f <nb>.ipynb` は**セルがエラーになっても後続セルを実行し続ける**。
CLI の標準出力だけでは成功に見えるため、実行後に必ず本スクリプトで確認する。

エラーがあれば終了コード 1 を返すので、シェルスクリプトからも判定できる。
"""
import json
import sys

if len(sys.argv) != 2:
    print(__doc__)
    sys.exit(2)

path = sys.argv[1]
with open(path, encoding="utf-8") as f:
    nb = json.load(f)

errors = 0
for i, cell in enumerate(nb.get("cells", [])):
    if cell.get("cell_type") != "code":
        continue
    head = "".join(cell.get("source", []))[:50].replace("\n", " ")
    kinds = [o.get("output_type") for o in cell.get("outputs", [])]
    errs = [o for o in cell.get("outputs", []) if o.get("output_type") == "error"]
    mark = "★ERROR" if errs else "      "
    print(f"{mark} [{i}] {head!r:55s} outputs={kinds}")
    for e in errs:
        errors += 1
        print(f"        {e.get('ename')}: {str(e.get('evalue'))[:120]}")

print()
if errors:
    print(f"エラーのあるセル: {errors} 件")
    sys.exit(1)
print("エラーなし")
