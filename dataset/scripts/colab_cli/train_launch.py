"""Colab VM 上で darknet 学習をバックグラウンド起動する（★検証専用）。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/train_launch.py --timeout 300

★用途の限定: 本スクリプトは「環境が正しく組み上がっているか」の疎通確認用である。
  #148 Phase 3 のような本番学習には使わないこと。本番の実行順序は
  doc/report/f003_03j_person_detection_phase3_plan.md の 5.5 節が正であり、
  データセットクリーニング(Step 2.5) / hard negative mining(Step 2.6) /
  アンカー再計算(Step 4) / 事前学習重み(Step 5) / Drive への checkpoint 退避 が必要になる。
  これらは元ノートブック train_yolo_fastest_darknet_colab.ipynb に一式そろっているため、
  本番学習はノートブックをそのまま実行すること（colab exec -f で .ipynb を直接渡せる）。

★安全設計: Google Drive には一切書き込まない
  元ノートブック train_yolo_fastest_darknet_colab.ipynb の cell[25] は、darknet の保存先を
  Drive へのシンボリックリンクに置き換え、`.issue137_started` があれば `_last.weights` から
  学習を再開する。短いテスト実行でこれを踏むと #148 Phase 3 の重みを上書きするため、
  本スクリプトは cell[25] を使わず、保存先を隔離した obj.data を生成して使う。

前提: setup_colab.py の完了後、**準備セルの実行まで済ませておくこと**。
      darknet 用の obj.data は元ノートブック cell[8]、cfg は cell[14][15][16] が生成する。
      setup_colab.py はディレクトリを作るだけでこれらのファイルは作らないため、
      間に make_prep_nb.py で生成したサブノートブックを実行する必要がある:

          python3 dataset/scripts/colab_cli/make_prep_nb.py prep_cells.ipynb
          colab --auth=adc exec -s <name> -f prep_cells.ipynb --timeout 900
          python3 dataset/scripts/colab_cli/check_notebook_errors.py prep_cells_output.ipynb

実行後は drive_guard.py で Drive が無傷であることを確認すること。
"""
import os
import re
import shlex
import subprocess
import sys

# --- 設定 ---
DARKNET_DIR = "/content/Yolo-Fastest"
BACKUP_DIR = "/content/backup_smoke"      # 保存先。Drive の外に置くこと
SRC_CFG = os.path.join(DARKNET_DIR, "cfg", "yolo-fastest-person-192.cfg")
SRC_DATA = os.path.join(DARKNET_DIR, "data", "person", "obj.data")
LOG_PATH = "/content/train.log"

# 検証用に max_batches を縮小する。None にすると cfg の値をそのまま使う。
#
# ★None にしても「本番学習」にはならない。
#   cfg の max_batches はそのまま使われるが、データセットのクリーニング・hard negative
#   mining・アンカー再計算が行われず、checkpoint も VM 上に残るだけになるため、
#   Phase 3 手順書 5.5 節が定める実験とは別物になる。長時間の実験を無駄にしないこと。
#   本番学習は元ノートブックを実行する（本ファイル冒頭の説明を参照）。
MAX_BATCHES = 100

# 転移学習の起点となる重み。空文字ならスクラッチ学習。
# DARKNET_DIR からの相対パスまたは絶対パスで指定する。
# 例: "yolo-fastest-coco-pretrained.weights"（元ノートブック cell[23] が生成）
#     "/content/drive/MyDrive/yolo_fastest_darknet_person/backup/xxx_final.weights"
# 空白を含むパスも扱えるよう、コマンド組み立て時に shlex.quote でクォートする。
PRETRAINED_WEIGHTS = ""

# --- 安全確認: darknet の backup が Drive を指していないこと ---
bk = os.path.join(DARKNET_DIR, "backup")
if os.path.islink(bk):
    real = os.path.realpath(bk)
    print("backup symlink ->", real)
    if "/drive/" in real:
        print("ERROR: backup が Drive を指しています。cell[25] が実行済みです。中止します。")
        sys.exit(1)
else:
    print("backup: Drive への symlink ではありません OK")

for p in (SRC_CFG, SRC_DATA):
    if not os.path.isfile(p):
        print("ERROR: 見つかりません:", p)
        print("  setup_colab.py はディレクトリを作るだけで、これらのファイルは作りません。")
        print("  先に準備セル（元ノートブック cell[8] / cell[14-16]）を実行してください:")
        print("    python3 dataset/scripts/colab_cli/make_prep_nb.py prep_cells.ipynb")
        print("    colab --auth=adc exec -s <name> -f prep_cells.ipynb --timeout 900")
        print("    python3 dataset/scripts/colab_cli/check_notebook_errors.py prep_cells_output.ipynb")
        sys.exit(1)

# --- 転移学習の起点となる重みの確認 ---
weights_arg = ""
clear_arg = ""
if PRETRAINED_WEIGHTS:
    wp = PRETRAINED_WEIGHTS
    if not os.path.isabs(wp):
        wp = os.path.join(DARKNET_DIR, wp)
    if not os.path.isfile(wp):
        print("ERROR: 事前学習重みが見つかりません:", wp)
        sys.exit(1)
    weights_arg = " " + shlex.quote(PRETRAINED_WEIGHTS)
    # 元ノートブック cell[26] と同じ判断。
    # finetune の起点として別の重みを渡す場合、seen カウンタが max_batches を超えていると
    # 「既に完了」と判定され学習ループが 0 回になるため -clear でリセットする。
    # 中断再開（_last.weights）ではカウンタを保持したいので付けない。
    if "_last.weights" not in PRETRAINED_WEIGHTS:
        clear_arg = " -clear"
    print(f"転移学習の起点: {PRETRAINED_WEIGHTS}{' (-clear あり)' if clear_arg else ' (-clear なし / 再開)'}")
elif MAX_BATCHES is None:
    print("WARNING: MAX_BATCHES=None かつ PRETRAINED_WEIGHTS が未設定です。")
    print("         cfg は finetune 前提のためスクラッチ学習になります。")
    print("         なお本スクリプトは検証専用です。Phase 3 の本番学習は元ノートブックを使ってください。")
else:
    print("事前学習重みなし → スクラッチ学習（検証用途）")

os.makedirs(BACKUP_DIR, exist_ok=True)

# --- cfg: max_batches を縮小した複製を作る ---
cfg_name = "yolo-fastest-person-192.cfg"
if MAX_BATCHES is not None:
    cfg = open(SRC_CFG).read()
    orig = re.search(r"max_batches\s*=\s*(\d+)", cfg)
    cfg = re.sub(r"max_batches\s*=\s*\d+", f"max_batches={MAX_BATCHES}", cfg)
    cfg_name = f"smoke-{MAX_BATCHES}.cfg"
    open(os.path.join(DARKNET_DIR, "cfg", cfg_name), "w").write(cfg)
    print(f"cfg: max_batches {orig.group(1) if orig else '?'} -> {MAX_BATCHES} ({cfg_name})")

# --- obj.data: backup 先を隔離した複製を作る ---
data_name = "obj_isolated.data"
out = []
for line in open(SRC_DATA):
    if line.strip().startswith("backup"):
        line = f"backup = {BACKUP_DIR}\n"
    out.append(line)
open(os.path.join(DARKNET_DIR, "data", "person", data_name), "w").writelines(out)
print("--- 使用する obj.data ---")
print(open(os.path.join(DARKNET_DIR, "data", "person", data_name)).read().strip())

# --- 起動 ---
# 完了マーカーは終了コード 0 のときだけ出す。
# 無条件に出すと OOM・cfg 不正・データセット異常で落ちた場合でも poll.py が完了と判定してしまう。
#
# 設定値由来のパスは空白を含みうるため、すべて shlex.quote でクォートしてから埋め込む。
darknet_dir_sh = shlex.quote(DARKNET_DIR)
backup_dir_sh = shlex.quote(BACKUP_DIR)
data_path_sh = shlex.quote(os.path.join("data", "person", data_name))
cfg_path_sh = shlex.quote(os.path.join("cfg", cfg_name))

sh = f"""#!/bin/bash
cd {darknet_dir_sh}
./darknet detector train {data_path_sh} {cfg_path_sh}{weights_arg} -dont_show -gpus 0{clear_arg}
rc=$?
echo "TRAIN EXIT CODE: $rc"
ls -la {backup_dir_sh}
if [ "$rc" -eq 0 ]; then
  echo "TRAIN DONE"
else
  echo "TRAIN FAILED (exit=$rc)"
fi
"""
open("/content/train.sh", "w").write(sh)
os.chmod("/content/train.sh", 0o755)

proc = subprocess.Popen(
    ["/content/train.sh"],
    stdout=open(LOG_PATH, "w"),
    stderr=subprocess.STDOUT,
    start_new_session=True,
)
print("launched pid:", proc.pid)
print("進捗は poll.py で確認してください:", LOG_PATH)
