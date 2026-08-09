"""Colab VM 上で darknet 学習をバックグラウンド起動する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f train_launch.py --timeout 300

★安全設計: Google Drive には一切書き込まない
  元ノートブック train_yolo_fastest_darknet_colab.ipynb の cell[25] は、darknet の保存先を
  Drive へのシンボリックリンクに置き換え、`.issue137_started` があれば `_last.weights` から
  学習を再開する。短いテスト実行でこれを踏むと #148 Phase 3 の重みを上書きするため、
  本スクリプトは cell[25] を使わず、保存先を隔離した obj.data を生成して使う。

前提: setup_colab.py の完了後、darknet 用の obj.data / cfg が生成済みであること
      （obj.data は元ノートブック cell[8]、cfg は cell[14][15][16] が生成する。
        make_prep_nb.py でそれらのセルだけを抜き出して実行できる）

実行後は drive_guard.py で Drive が無傷であることを確認すること。
"""
import os
import re
import subprocess
import sys

# --- 設定 ---
DARKNET_DIR = "/content/Yolo-Fastest"
BACKUP_DIR = "/content/backup_smoke"      # 保存先。Drive の外に置くこと
SRC_CFG = os.path.join(DARKNET_DIR, "cfg", "yolo-fastest-person-192.cfg")
SRC_DATA = os.path.join(DARKNET_DIR, "data", "person", "obj.data")
MAX_BATCHES = 100                          # 本番学習では None にして cfg の値をそのまま使う
LOG_PATH = "/content/train.log"

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
        print("  元ノートブックの cell[8] / cell[14-16] を実行してください（make_prep_nb.py 参照）")
        sys.exit(1)

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
sh = f"""#!/bin/bash
cd {DARKNET_DIR}
./darknet detector train data/person/{data_name} cfg/{cfg_name} -dont_show -gpus 0
echo "TRAIN EXIT CODE: $?"
ls -la {BACKUP_DIR}
echo "TRAIN DONE"
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
