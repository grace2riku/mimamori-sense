"""元の学習ノートブックから指定セルだけを抜き出したサブノートブックを生成する。

実行場所: ローカル（WSL）
使い方  : python3 make_prep_nb.py [出力先.ipynb]

生成したノートブックは次のように VM 上で実行する:
    colab --auth=adc exec -s trainer -f prep_cells.ipynb --timeout 900
実行後は check_notebook_errors.py で prep_cells_output.ipynb を検査すること。

★セルを抜き出すときの注意
  飛ばしたセルの副作用（ディレクトリ作成など）が引き継がれない点に注意する。
  例: cell[7] は data/person と backup を作成しており、これを飛ばすと cell[8] が
      FileNotFoundError になる。setup_colab.py はこのディレクトリ作成を代行している。

★除外すべきセル
  cell[25] は darknet の保存先を Drive へ symlink し、学習を再開する設計になっている。
  検証目的の実行で踏むと #148 Phase 3 の重みを上書きするため、含めてはならない。
"""
import json
import os
import sys

SRC = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..",
    "train_yolo_fastest_darknet_colab.ipynb",
)

# 抜き出すセル番号
#   [8]  darknet 用ラベル配置(symlink) / train.txt / valid.txt / obj.data / obj.names
#   [14] リポジトリ内 cfg の一覧
#   [15] yolo-fastest-person-192.cfg を直接生成
#   [16] リポジトリの基準 cfg をベースに再生成（15 を上書き）
# 除外したセル
#   [10] データセットクリーニング, [12] hard negative mining, [18-20] アンカー再計算,
#   [22-23] 事前学習重み取得 … いずれも精度目的。疎通確認では不要
#   [25] backup を Drive へ symlink + 再開ロジック … ★Drive の重みを壊す
PICK = [8, 14, 15, 16]

dst = sys.argv[1] if len(sys.argv) > 1 else "prep_cells.ipynb"

with open(os.path.normpath(SRC), encoding="utf-8") as f:
    nb = json.load(f)

cells = []
for i in PICK:
    cell = dict(nb["cells"][i])
    src = "".join(cell["source"])
    cell["source"] = [f"# @title 元ノートブック cell[{i}]\n", src]
    cell["outputs"] = []
    cell["execution_count"] = None
    cell["id"] = f"prep{i}"
    cells.append(cell)
    print(f"cell[{i}]: {len(src)} chars")

with open(dst, "w", encoding="utf-8") as f:
    json.dump(
        {
            "cells": cells,
            "metadata": nb.get("metadata", {}),
            "nbformat": 4,
            "nbformat_minor": 5,
        },
        f,
        ensure_ascii=False,
    )

print("wrote:", dst)
