# colab_cli スクリプト群

google-colab-cli で Colab 上のモデル学習を回すための補助スクリプト。
環境構築と運用の全体像は `doc/colab-cli-setup-guide/colab-cli-setup-guide.md` を参照。

## 実行場所の区別

パスはすべて**リポジトリルートからの相対**。リポジトリルートで実行すること。

| ファイル | 実行場所 | 起動方法 |
|----------|---------|---------|
| `setup_colab.py` | Colab VM | `colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/setup_colab.py --timeout 300` |
| `train_launch.py` | Colab VM | 同上（ファイル名を差し替え） |
| `poll.py` | Colab VM | 同上 |
| `drive_guard.py` | Colab VM | 同上 |
| `make_prep_nb.py` | ローカル（WSL） | `python3 dataset/scripts/colab_cli/make_prep_nb.py [出力先.ipynb]` |
| `check_notebook_errors.py` | ローカル（WSL） | `python3 dataset/scripts/colab_cli/check_notebook_errors.py <nb>_output.ipynb` |

VM 側で動くスクリプトは `colab exec -f` でファイル内容が送られて実行される。
**コマンドライン引数は渡らない**ため、設定はファイル先頭の定数を編集して使う。

## 典型的な流れ

以下はすべて**リポジトリルート**で実行する（スクリプトのパスはリポジトリルートからの相対）。

```bash
cd /mnt/c/Users/<user>/github/mimamori-sense

# ① セッション作成
colab --auth=adc new -s trainer --gpu L4

# ② Drive マウント（TTY 必須。人が手動で実行し、承認後に Enter）
colab --auth=adc drivemount -s trainer

# ③ ビルド + データセット展開（バックグラウンド起動）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/setup_colab.py --timeout 300

# ④ 完了するまで繰り返す
#    "SETUP DONE" なら成功。"SETUP FAILED (exit=N)" なら中断されているのでログ末尾を確認する
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300

# ⑤ 準備セルの実行（obj.data と cfg を生成する。★この手順は省略できない）
#    setup_colab.py はディレクトリを作るだけで obj.data / cfg は作らないため、
#    これを飛ばすと ⑥ が「見つかりません」で終了する
python3 dataset/scripts/colab_cli/make_prep_nb.py prep_cells.ipynb
colab --auth=adc exec -s trainer -f prep_cells.ipynb --timeout 900
python3 dataset/scripts/colab_cli/check_notebook_errors.py prep_cells_output.ipynb

# ⑥ 学習をバックグラウンド起動
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/train_launch.py --timeout 300

# ⑦ 進捗確認（PC を閉じても学習は継続する）
#    "TRAIN DONE" なら成功、"TRAIN FAILED (exit=N)" なら失敗
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300

# ⑧ Drive を汚していないことの確認（検証実行時）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/drive_guard.py --timeout 300

# ⑨ 成果物の回収とセッション停止
#    パスは train_launch.py の BACKUP_DIR と揃えること（既定は /content/backup_smoke）
colab --auth=adc download -s trainer /content/backup_smoke/xxx_final.weights ./xxx_final.weights
colab --auth=adc stop -s trainer
```

## 注意

- `--auth=adc` は**サブコマンドより前**に置く（グローバルフラグ）
- `colab exec` の既定タイムアウトは **30 秒**。同期処理には `--timeout` を明示する
- **セッションの停止を忘れないこと**（24 時間まで自動解放されない）
- **⑤ の準備セル実行は省略できない**。`setup_colab.py` が作るのはディレクトリだけで、
  `obj.data` と cfg は元ノートブックの cell[8] / cell[14-16] が生成する
- `train_launch.py` は元ノートブックの cell[25]（`backup` を Drive へ symlink）を**使わない**。
  検証実行で Phase 3 の重みを上書きしないための隔離設計になっている
- **⚠️ 本スクリプト群は疎通確認専用。本番学習には使わないこと。**
  `make_prep_nb.py` が抜き出すのは cell[8] / cell[14-16] のみで、データセットクリーニング
  (cell[10]) / hard negative mining (cell[12]) / アンカー再計算 (cell[18-20]) /
  事前学習重み (cell[22-23]) / Drive への checkpoint 退避 (cell[25]) を含まない。
  これらは Phase 3 手順書 5.5 節で本番学習に必須とされている。
  本番学習は元ノートブックをそのまま実行する:
  `colab --auth=adc exec -s trainer -f dataset/scripts/train_yolo_fastest_darknet_colab.ipynb --timeout 900`
- 成果物のダウンロード先パスは `train_launch.py` の `BACKUP_DIR` と揃えること。
  `BACKUP_DIR` は VM 上の一時領域のため、**`colab stop` の前に回収する**
