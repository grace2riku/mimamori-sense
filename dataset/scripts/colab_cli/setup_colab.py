"""Colab VM 上で darknet ビルドとデータセット展開をバックグラウンド実行する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/setup_colab.py --timeout 300

事前に `colab --auth=adc drivemount -s <name>` を済ませておくこと。
進捗は poll.py で確認する。完了マーカーは "SETUP DONE"。

処理内容（元ノートブック train_yolo_fastest_darknet_colab.ipynb の cell[3][4][7] 相当）:
  1. Yolo-Fastest を clone
  2. Makefile を GPU=1 / CUDNN=1 / OPENCV=0 に書き換えて make
  3. Drive 上のデータセット zip を /content/dataset へ展開
  4. cell[7] が作っていたディレクトリ（data/person, backup）を作成
"""
import os
import shlex
import subprocess
import sys

# --- 設定 ---
DRIVE_ZIP = "/content/drive/MyDrive/fall_detection_dataset.zip"
DARKNET_DIR = "/content/Yolo-Fastest"
DATASET_DIR = "/content/dataset"
LOG_PATH = "/content/setup.log"

# --- 事前チェック（ここで落とせば VM 時間を無駄にしない） ---
if not os.path.isdir("/content/drive/MyDrive"):
    print("ERROR: Drive が未マウントです。先に `colab --auth=adc drivemount -s <name>` を実行してください。")
    sys.exit(1)

if not os.path.isfile(DRIVE_ZIP):
    print("ERROR: データセットが見つかりません:", DRIVE_ZIP)
    sys.exit(1)

print("dataset zip: {:,} bytes".format(os.path.getsize(DRIVE_ZIP)))

# set -eu で途中終了した場合も必ず失敗マーカーを残す。
# マーカーが無いと poll.py 側で「失敗」と「まだ実行中」を区別できない。
script = f"""#!/bin/bash
# pipefail が無いと `make ... | tail` の終了コードが tail のもの(0)になり、
# ビルド失敗を set -e も EXIT trap も検出できない。
# 再実行時に古い darknet バイナリが残っていると ls も通ってしまい、
# 失敗したビルドのまま SETUP DONE が出てしまう。
set -euo pipefail

finish() {{
  rc=$?
  if [ "$rc" -ne 0 ]; then
    echo "SETUP FAILED (exit=$rc)"
  fi
}}
trap finish EXIT

DARKNET_DIR={shlex.quote(DARKNET_DIR)}
DATASET_DIR={shlex.quote(DATASET_DIR)}
ZIP={shlex.quote(DRIVE_ZIP)}

echo "=== [1/4] Yolo-Fastest clone ==="
if [ -d "$DARKNET_DIR" ]; then
  echo "既に存在: $DARKNET_DIR"
else
  git clone --depth 1 https://github.com/dog-qiuqiu/Yolo-Fastest.git "$DARKNET_DIR"
fi

echo "=== [2/4] darknet build (GPU=1 CUDNN=1 OPENCV=0) ==="
cd "$DARKNET_DIR"
sed -i 's/GPU=0/GPU=1/' Makefile
sed -i 's/CUDNN=0/CUDNN=1/' Makefile
sed -i 's/OPENCV=1/OPENCV=0/' Makefile
grep -E '^(GPU|CUDNN|OPENCV)=' Makefile
make clean > /dev/null 2>&1 || true
make -j"$(nproc)" 2>&1 | tail -20
ls -la ./darknet
echo "darknet ビルド成功"

echo "=== [3/4] dataset 展開 ==="
# 既存ディレクトリへ unzip -o で上書きすると、新しい zip に含まれない古い画像・ラベルが
# 残り続ける。再実行時や zip を差し替えたとき（データ拡充・クリーニング後など）に
# 複数バージョンのデータセットが混ざったまま学習が走るため、
# 必ず空のステージングディレクトリへ展開し、検証を通ってから入れ替える。
STAGE="$DATASET_DIR.staging"
rm -rf "$STAGE"
mkdir -p "$STAGE"
# unzip は警告（Windows 製 zip のバックスラッシュ区切り等）で終了コード 1 を返す。
# set -e で中断しないよう 1 までは許容する。
time unzip -q "$ZIP" -d "$STAGE" || [ $? -le 1 ]

# 検証はステージング側で行う。不正なら既存のデータセットを壊さずに終了できる。
# 件数取得そのものはディレクトリ欠損で止めない（pipefail 下では ls の失敗が
# パイプライン全体に伝播するため || true を付ける）。判定は下のチェックで行う。
# 数える対象は cell[8] が train.txt 生成に使う条件（*.jpg / *.png / *.jpeg）に合わせる。
# ディレクトリや Thumbs.db 等を数えてしまうと、実質空の split でもチェックを通過し、
# 空の train.txt が作られたまま学習に進んでしまう。
IMG_RE='.*[.](jpg|png|jpeg)'
layout_ng=0
for s in train val test; do
  ic=$(find "$STAGE/images/$s" -maxdepth 1 -type f -regextype posix-extended -regex "$IMG_RE" 2>/dev/null | wc -l || true)
  lc=$(find "$STAGE/labels/$s" -maxdepth 1 -type f -name '*.txt' 2>/dev/null | wc -l || true)
  echo "  $s: images=$ic labels=$lc"
  if [ "$ic" -eq 0 ] || [ "$lc" -eq 0 ]; then
    echo "  ERROR: $s に利用可能な画像/ラベルがありません（*.jpg *.png *.jpeg と *.txt を対象）"
    layout_ng=1
    continue
  fi

  # 画像とラベルの対応を双方向で確認する。
  # dataset/scripts/quality_check.py の Check 3 は images without labels /
  # labels without images をいずれも FAIL としており、それに合わせる。
  # 意図的な負例は hard_negative_mining.py が「空ラベル(.txt)付き」で追加するため
  # ラベルファイル自体は存在する。したがって欠損は許容しない。
  img_stems=$(mktemp)
  lbl_stems=$(mktemp)
  find "$STAGE/images/$s" -maxdepth 1 -type f -regextype posix-extended -regex "$IMG_RE" | sed 's|.*/||; s|[.][^.]*$||' | sort > "$img_stems"
  find "$STAGE/labels/$s" -maxdepth 1 -type f -name '*.txt' | sed 's|.*/||; s|[.]txt$||' | sort > "$lbl_stems"

  nolabel=$(comm -23 "$img_stems" "$lbl_stems" | wc -l)
  if [ "$nolabel" -ne 0 ]; then
    echo "  ERROR: $s にラベルの無い画像が $nolabel 件あります"
    comm -23 "$img_stems" "$lbl_stems" | head -3 | sed 's|^|    例: |'
    layout_ng=1
  fi

  noimage=$(comm -13 "$img_stems" "$lbl_stems" | wc -l)
  if [ "$noimage" -ne 0 ]; then
    echo "  ERROR: $s に画像の無いラベルが $noimage 件あります"
    comm -13 "$img_stems" "$lbl_stems" | head -3 | sed 's|^|    例: |'
    layout_ng=1
  fi

  rm -f "$img_stems" "$lbl_stems"
done

# zip 自体は展開できても、ルート構成が想定と異なる場合や、警告(終了コード1)を
# 許容した結果として一部の split が欠ける場合がある。
# ここで検出せずに SETUP DONE を出すと、準備セルや学習が後段で失敗する。
if [ "$layout_ng" -ne 0 ]; then
  echo "ERROR: データセットの展開結果に問題があります。zip を確認してください。"
  echo "       - ルート直下に images/{{train,val,test}} と labels/{{train,val,test}} があること"
  echo "       - 画像(*.jpg *.png *.jpeg)とラベル(*.txt)がファイル名で 1 対 1 に対応すること"
  echo "         （負例は空の .txt を置く。ラベルファイル自体の欠損は不可）"
  rm -rf "$STAGE"
  exit 1
fi

# 検証を通ってから入れ替える（同一ファイルシステム上の rename なので瞬時）。
rm -rf "$DATASET_DIR.old"
if [ -d "$DATASET_DIR" ]; then
  mv "$DATASET_DIR" "$DATASET_DIR.old"
fi
mv "$STAGE" "$DATASET_DIR"
rm -rf "$DATASET_DIR.old"
echo "データセットを入れ替えました: $DATASET_DIR"
df -h /content | tail -1

echo "=== [4/4] darknet 用ディレクトリ作成（元ノートブック cell[7] 相当） ==="
mkdir -p "$DARKNET_DIR/data/person" "$DARKNET_DIR/backup"
echo "created: $DARKNET_DIR/data/person"

echo "SETUP DONE"
"""

with open("/content/setup.sh", "w") as f:
    f.write(script)
os.chmod("/content/setup.sh", 0o755)

proc = subprocess.Popen(
    ["/content/setup.sh"],
    stdout=open(LOG_PATH, "w"),
    stderr=subprocess.STDOUT,
    start_new_session=True,  # カーネルのプロセスグループから切り離す（これが要点）
)
print("launched pid:", proc.pid)
print("進捗は poll.py で確認してください:", LOG_PATH)
