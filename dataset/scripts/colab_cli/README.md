# colab_cli スクリプト群

google-colab-cli で Colab 上のモデル学習を回すための補助スクリプト。
環境構築と運用の全体像は `doc/colab-cli-setup-guide/colab-cli-setup-guide.md` を参照。

## 実行場所の区別

| ファイル | 実行場所 | 起動方法 |
|----------|---------|---------|
| `setup_colab.py` | Colab VM | `colab --auth=adc exec -s <name> -f setup_colab.py --timeout 300` |
| `train_launch.py` | Colab VM | 同上 |
| `poll.py` | Colab VM | 同上 |
| `drive_guard.py` | Colab VM | 同上 |
| `make_prep_nb.py` | ローカル（WSL） | `python3 make_prep_nb.py` |
| `check_notebook_errors.py` | ローカル（WSL） | `python3 check_notebook_errors.py <nb>_output.ipynb` |

VM 側で動くスクリプトは `colab exec -f` でファイル内容が送られて実行される。
**コマンドライン引数は渡らない**ため、設定はファイル先頭の定数を編集して使う。

## 典型的な流れ

```bash
# ① セッション作成
colab --auth=adc new -s trainer --gpu L4

# ② Drive マウント（TTY 必須。人が手動で実行し、承認後に Enter）
colab --auth=adc drivemount -s trainer

# ③ ビルド + データセット展開（バックグラウンド起動）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/setup_colab.py --timeout 300

# ④ 完了するまで繰り返す（"SETUP DONE" が出れば完了）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300

# ⑤ 学習をバックグラウンド起動
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/train_launch.py --timeout 300

# ⑥ 進捗確認（PC を閉じても学習は継続する）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300

# ⑦ Drive を汚していないことの確認（検証実行時）
colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/drive_guard.py --timeout 300

# ⑧ 成果物の回収とセッション停止
colab --auth=adc download -s trainer /content/backup_smoke/xxx.weights ./xxx.weights
colab --auth=adc stop -s trainer
```

## 注意

- `--auth=adc` は**サブコマンドより前**に置く（グローバルフラグ）
- `colab exec` の既定タイムアウトは **30 秒**。同期処理には `--timeout` を明示する
- **セッションの停止を忘れないこと**（24 時間まで自動解放されない）
- `train_launch.py` は元ノートブックの cell[25]（`backup` を Drive へ symlink）を**使わない**。
  検証実行で Phase 3 の重みを上書きしないための隔離設計になっている
