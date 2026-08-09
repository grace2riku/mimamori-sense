# google-colab-cli 学習環境 構築・運用ガイド

Issue #179。Windows 11 の開発 PC から、ブラウザを介さずに Google Colab の GPU ランタイムで
モデル学習を回すための環境構築手順と運用方法をまとめる。

対象読者は本リポジトリでモデル学習（#148 ほか）を行う開発者。

---

## 1. 概要

### 何ができるようになるか

- VS Code / ターミナルから Colab の GPU ランタイムを直接操作できる
- 学習をバックグラウンドで走らせ、PC を閉じても継続できる
- 学習ログ・重みをコマンドでローカルへ回収できる

### 構成

google-colab-cli は **Linux / macOS のみ対応で Windows は非対応**のため、WSL2 上で動かす。
リポジトリは Windows 側の 1 コピーのみとし、WSL からは `/mnt/c` 経由で参照する。

```
┌─ Windows 11 ────────────────────────────────────────┐
│  C:\Users\<user>\github\mimamori-sense              │
│      ↑ リポジトリはここ 1 つだけ                      │
│      ↑ e2 studio・git・Claude Code はここで動かす      │
│                                                     │
│  ┌─ WSL2 / Ubuntu ─────────────────────────┐        │
│  │  /mnt/c/Users/<user>/github/mimamori-sense│       │
│  │  colab コマンドはここで動かす              │        │
│  └──────────────────────────────────────────┘        │
└─────────────────────────────────────────────────────┘
                       │  colab new / exec / download / stop
                       ▼
              Google Colab ランタイム（GPU: L4）
```

### 設計判断

| 論点 | 結論 | 理由 |
|------|------|------|
| リポジトリの置き場所 | `/mnt/c` 参照（Windows 側 1 コピー） | 読み出し 129 MB/s を実測しボトルネックにならないと確認。2 コピーはコミット先が分散する。e2 studio は Windows 側の実体が必要 |
| git の実行場所 | **Windows 側のみ** | WSL から `/mnt/c` の git を触ると dubious ownership 警告や改行コードの差異が出る |
| Claude Code の実行場所 | Windows 側のまま | `wsl -e bash -lc "colab ..."` で WSL 側のコマンドを呼べる |
| 認証方式 | **ADC**（`--auth=adc` を毎回明示） | oauth2 は自前の OAuth クライアント JSON が必要で手間が大きい |
| データセットの受け渡し | **Drive マウント** | 実サイズ 4.87 GB。分割アップロードだと 78 分割・約 27 分かかる |
| 長時間ジョブの実行方式 | **VM 側バックグラウンド実行 + ログ追跡** | `colab exec` の既定タイムアウトは 30 秒。本番学習は約 2.5 時間 |

---

## 2. 前提

| 項目 | バージョン（構築時の実測） |
|------|--------------------------|
| Windows | 11 Pro |
| WSL | 2.7.11 |
| Ubuntu | 26.04 LTS |
| Python（Ubuntu 側） | 3.14.4（要件は 3.12 以上） |
| uv | 0.12.3 |
| google-colab-cli | **0.6.0** |
| Google Cloud SDK | 579.0.0 |
| Colab 側 Python | 3.12.13 |

Colab の GPU（L4）を割り当てられるサブスクリプションが必要。

---

## 3. 環境構築手順

表記: **【Win】** = Windows の PowerShell / **【WSL】** = Ubuntu のターミナル

### STEP 1: WSL2 と Ubuntu の導入【Win】

PowerShell を**管理者として実行**で開く。

```powershell
wsl --install
```

WSL 本体と VirtualMachinePlatform が入る。完了後 **Windows を再起動**。

> **注意: `wsl --install` だけでは Ubuntu は入らない。** WSL 本体のインストールと再起動が先に必要。

再起動後、あらためて実行する。

```powershell
wsl -l -v                 # 「ディストリビューションはありません」と出るはず
wsl --install -d Ubuntu
```

Ubuntu が起動し、**Linux 用のユーザー名とパスワード**を聞かれる（Windows アカウントとは別。`sudo` で使用）。
パスワード入力中は画面に何も表示されない。

```powershell
wsl -l -v                 # Ubuntu / Running / 2 なら成功
```

### STEP 2: uv の導入【WSL】

Ubuntu 26.04 には curl と Python 3.14 が同梱されているため、**`sudo apt` は不要**。

```bash
curl -LsSf https://astral.sh/uv/install.sh | sh
source ~/.bashrc
uv --version
```

### STEP 3: google-colab-cli の導入【WSL】

```bash
uv tool install --with "jupyter-kernel-client<1" google-colab-cli
colab version          # ← --version ではない。Version: 0.6.0 が出れば OK
```

> **`--with "jupyter-kernel-client<1"` は必須。** 理由は「6.1 依存パッケージの固定」を参照。

`colab: command not found` の場合は PATH を通す。

```bash
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc && source ~/.bashrc
```

### STEP 4: Google Cloud SDK の導入【WSL】

ADC 認証に必要。sudo 不要でホームディレクトリに入れる。

```bash
cd ~
curl -sSLO https://dl.google.com/dl/cloudsdk/channels/rapid/downloads/google-cloud-cli-linux-x86_64.tar.gz
tar -xzf google-cloud-cli-linux-x86_64.tar.gz
rm google-cloud-cli-linux-x86_64.tar.gz
./google-cloud-sdk/install.sh --quiet --path-update true --command-completion true
```

PATH 設定が `~/.bashrc` に書かれるため、**ターミナルを開き直す**（または `source ~/.bashrc`）。

```bash
gcloud --version
```

### STEP 5: ADC 認証【WSL】

Colab バックエンドが要求する **4 つのスコープをすべて指定**して ADC を発行する。

```bash
gcloud auth application-default login \
  --scopes=openid,https://www.googleapis.com/auth/cloud-platform,https://www.googleapis.com/auth/userinfo.email,https://www.googleapis.com/auth/colaboratory \
  --no-launch-browser
```

| スコープ | 欠けたときの症状 |
|---------|----------------|
| `userinfo.email` | セッションバックエンドが **401** |
| `colaboratory` | キープアライブ RPC が **403**（`keep_alive_stopped` でセッションが落ちる） |
| `openid` + `cloud-platform` | gcloud 自体がスコープ指定を拒否する |

手順:

1. 表示された URL を **Windows のブラウザ**に貼って開く
2. Colab を使っている Google アカウントを選び、承認する
3. 表示された認証コードを「Copy」ボタンでコピーし、**寄り道せず直ちに**ターミナルへ貼る
4. `Credentials saved to file: [~/.config/gcloud/application_default_credentials.json]` が出れば成功

> **`--no-launch-browser` は必須。** WSL にはブラウザを開く手段がなく、素のコマンドは
> `gio: ... Operation not supported` で失敗する。
>
> **認証コードは数分で失効する。** 別の場所に貼るなど寄り道すると
> `(invalid_grant) Bad Request` になる。

確認:

```bash
colab --auth=adc whoami     # Email と 4 スコープが出る
colab --auth=adc sessions   # エラーなく返れば成功（一覧が空でも可）
```

---

## 4. 運用フロー

学習 1 周の流れ。②のみ人手が必要で、③以降は自動化できる。

**以下はすべてリポジトリルートで実行する。** 環境構築（STEP 4）の直後はホームディレクトリに
いるため、まず移動すること。スクリプトのパスはリポジトリルートからの相対パスで示す。

```bash
cd /mnt/c/Users/<user>/github/mimamori-sense
```

```
【WSL】① colab --auth=adc new -s trainer --gpu L4
【WSL】② colab --auth=adc drivemount -s trainer          ← TTY 必須。人が 1 回だけ実行
【WSL】③ colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/setup_colab.py --timeout 300
              # Yolo-Fastest clone + darknet ビルド + データセット展開（バックグラウンド起動）
【WSL】④ colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300
              # "SETUP DONE" が出るまで繰り返す
              # "SETUP FAILED (exit=N)" が出たら中断されている。ログ末尾で原因を確認する
【WSL】⑤ python3 dataset/scripts/colab_cli/make_prep_nb.py prep_cells.ipynb     ← ★省略不可
        colab --auth=adc exec -s trainer -f prep_cells.ipynb --timeout 900
        python3 dataset/scripts/colab_cli/check_notebook_errors.py prep_cells_output.ipynb
              # obj.data と cfg を生成する（元ノートブック cell[8] / cell[14-16]）
【WSL】⑥ colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/train_launch.py --timeout 300
              # 学習をバックグラウンド起動 → 即戻る
【WSL】⑦ colab --auth=adc exec -s trainer -f dataset/scripts/colab_cli/poll.py --timeout 300
              # 任意のタイミングでログ確認。PC を閉じても学習は継続
              # "TRAIN DONE" なら成功、"TRAIN FAILED (exit=N)" なら失敗
【WSL】⑧ colab --auth=adc download -s trainer /content/backup_smoke/xxx_final.weights ./xxx_final.weights
              # パスは train_launch.py の BACKUP_DIR と揃える。stop の前に回収すること
【WSL】⑨ colab --auth=adc stop -s trainer                 ← 必須
```

**⑤ を飛ばすと ⑥ が必ず失敗する。** `setup_colab.py` が作るのはディレクトリ
（`data/person` と `backup`）だけで、`obj.data` と cfg は元ノートブックのセルが生成する。

**⑧ は ⑨ より前に必ず行う。** `BACKUP_DIR`（既定 `/content/backup_smoke`）は VM 上の
一時領域のため、`colab stop` で VM ごと消える。

**`--auth=adc` はサブコマンドより前に置く**（グローバルフラグのため）。

```bash
colab --auth=adc new -s trainer --gpu L4     # OK
colab new --auth=adc -s trainer --gpu L4     # NG
```

### `drivemount` の挙動

```
[colab] REQUIRED: Google Drive Authorization needed.
Please visit:
  https://accounts.google.com/o/oauth2/v2/auth?...
Press Enter after you have granted access...      ← ここで人間の入力待ち
[colab] Authorizing VM...
Mounted at /content/drive
```

URL を Windows のブラウザで開いて承認し、ターミナルで **Enter** を押す。
この待ちがあるためエージェントからは実行できない。

### 停止を忘れないこと

セッションは 24 時間のキープアライブ上限まで自動解放されない。
`colab --auth=adc sessions` で確認し、不要なら `colab --auth=adc stop -s <name>`。

---

## 5. スクリプト

`dataset/scripts/colab_cli/` に配置。詳細は同ディレクトリの `README.md` を参照。

| ファイル | 実行場所 | 役割 |
|----------|---------|------|
| `setup_colab.py` | VM（`colab exec -f`） | clone + darknet ビルド + データセット展開をバックグラウンド起動 |
| `train_launch.py` | VM（`colab exec -f`） | Drive を汚さない隔離構成で学習を起動 |
| `poll.py` | VM（`colab exec -f`） | ログ追跡（VM 側で待ってから tail） |
| `drive_guard.py` | VM（`colab exec -f`） | Drive に書き込みが発生していないことを検証 |
| `make_prep_nb.py` | ローカル（WSL） | 元ノートブックから指定セルだけ抜き出したサブノートブックを生成 |
| `check_notebook_errors.py` | ローカル（WSL） | `*_output.ipynb` のセルエラーを検出 |

---

## 6. 落とし穴

### 6.1 依存パッケージの固定（必須）

**症状**: `colab new` / `status` / `stop` は動くが `colab exec` だけが落ちる。

```
AttributeError: module 'jupyter_kernel_client' has no attribute 'KernelClient'
  at colab_cli/runtime.py:106
```

**原因**: google-colab-cli 0.6.0 が `jupyter-kernel-client` を**バージョン指定なし**で要求しており、
同パッケージ 1.0.0 の破壊的変更（`KernelClient` → `JupyterKernelClient` に改名）を踏む。

| 日付 | 出来事 |
|------|--------|
| 2026-06-16 | google-colab-cli 0.6.0 リリース |
| 2026-07-24 | jupyter-kernel-client 0.15.0（0.x 系の最終） |
| 2026-07-26 | jupyter-kernel-client 1.0.0 — 改名 |
| 2026-08-08 | jupyter-kernel-client 1.0.1 |

**対処**: 0.x に固定する。**再インストールのたびに必要。**

```bash
uv tool install --force --with "jupyter-kernel-client<1" google-colab-cli
```

上流が依存を固定するか 1.x へ対応したら不要になる。0.6.0 より新しい版を入れる際は要確認。

### 6.2 `colab exec` の既定タイムアウトは 30 秒

```
--timeout <float>  Timeout in seconds for code execution [default: 30.0]
```

同期実行する処理（darknet ビルドなど）では `--timeout 300` 以上を明示する。
本番学習は約 2.5 時間かかるため、**必ずバックグラウンド起動**（`train_launch.py` の方式）にする。

### 6.3 `colab exec -f` はセルがエラーになっても停止しない

ノートブックを実行したとき、途中のセルが例外で落ちても**後続セルが実行され続ける**。
CLI の標準出力だけでは成功に見えるため、実行後に `<basename>_output.ipynb` を必ず確認する。

```bash
python3 dataset/scripts/colab_cli/check_notebook_errors.py <basename>_output.ipynb
```

さらに、**例外を投げずに処理を飛ばすセルもある**点に注意する。学習ノートブックの
cell[10]（データセットクリーニング）と cell[12]（hard negative mining）は、前提ファイルが
無いと `print('ERROR: ...')` / `print('SKIP: ...')` を出すだけで学習に進む。
`output_type: "error"` にならないため、上記スクリプトは出力テキスト中の
`ERROR:` / `SKIP:` / `WARNING:` も検出対象にしている（該当すれば終了コード 1）。

### 6.4 ノートブックのセルを部分実行すると暗黙の依存が壊れる

`train_yolo_fastest_darknet_colab.ipynb` では cell[7] が `data/person` や `backup` を作成しており、
これを飛ばすと cell[8] が `FileNotFoundError` になる。
セルを選んで実行する場合は、飛ばしたセルの副作用（ディレクトリ作成など）を引き継ぐこと。

### 6.5 ⚠️ cell[25] は Drive の学習成果物を上書きしうる

cell[25] は darknet の保存先を Drive へのシンボリックリンクに置き換え、
`.issue137_started` マーカーがあれば `_last.weights` から学習を再開する。

```python
os.symlink(gdrive_backup_dir, BACKUP_DIR)
# /content/Yolo-Fastest/backup -> /content/drive/MyDrive/yolo_fastest_darknet_person/backup
```

**短いテスト実行でこれを踏むと Phase 3 の重みが上書きされる。**
検証目的の学習では cell[25] を使わず、`train_launch.py` のように
`backup` の向き先を隔離した `obj.data` を使うこと。実行後は `drive_guard.py` で無傷を確認する。

### 6.6 `unzip` は警告時に終了コード 1 を返す

Windows で作成した zip は「バックスラッシュ区切り」警告を出し、`unzip` が 1 を返す。
`set -e` のシェルスクリプトではここで中断する（展開自体は成功しているのに後続が動かない）。

```bash
unzip -q -o "$ZIP" -d "$DST" || [ $? -le 1 ]
```

なお展開結果にバックスラッシュを含むファイル名は生成されず、構造は正しく復元される。

### 6.7 zip の破損は `file` コマンドでは検出できない

`file` は先頭ヘッダしか見ないため、途中で切れた zip でも `Zip archive data` と表示する。
zip の作成・取得の直後は必ず次で検証する。

```bash
python3 -c "import zipfile,sys; zipfile.ZipFile(sys.argv[1]); print('OK')" <zipファイル>
```

### 6.8 `colab upload` には 1 ファイルあたりのサイズ上限がある

| サイズ | 結果 |
|--------|------|
| 64 MB | 成功 |
| 80 MB 以上 | **400 Bad Request** |

base64 変換後 100 MB の制限と推定される。実効速度は約 3.05 MB/s。
64 MB 超のファイルは `split -b 64M` で分割して送り、VM 側で結合する。
データセット本体（4.87 GB）は Drive マウントを使うこと。

### 6.9 `upload` と `download` は引数の向きが逆

```
colab upload   {local_path}  {remote_path}
colab download {remote_path} {local_path}     ← リモートが先
```

### 6.10 ⚠️ 本スクリプト群は疎通確認専用。本番学習には元ノートブックを使う

`train_launch.py` と `make_prep_nb.py` は「環境が正しく組み上がっているか」を確かめるための
最小構成であり、**#148 Phase 3 のような本番学習には使えない**。

`make_prep_nb.py` が抜き出すのは cell[8] / cell[14-16] だけで、次のセルを含まない。

| 除外セル | Phase 3 手順書での位置づけ |
|---------|--------------------------|
| cell[10] データセットクリーニング | Step 2.5 |
| cell[12] hard negative mining | Step 2.6 |
| cell[18-20] アンカー再計算 | Step 4 |
| cell[22-23] 事前学習重み取得 | Step 5 |
| cell[25] backup を Drive へ退避 | 学習中の checkpoint 保全 |

いずれも疎通確認には不要だが、**本番学習では必須**である
（`doc/report/f003_03j_person_detection_phase3_plan.md` 5.5 節「実行順序」）。
これらを飛ばして 100,000 iteration の finetune を回すと、クリーニング前のデータと
既定アンカーで学習することになり、定義された実験とは別物になる。
さらに checkpoint が VM 上にしか残らないため、数時間の学習中に切断されると失われる。

**本番学習は元ノートブックをそのまま実行する。** 必要な手順が正しい順序で揃っている。
ただし実行前に次の 2 点を満たすこと。

#### (a) Drive 上に前提ファイルを配置する

チェックリストは `doc/report/f003_03j_person_detection_phase3_plan.md` 5.4 節が正。要点のみ再掲する。

| 配置先 | ファイル |
|--------|---------|
| `/content/drive/MyDrive/` | `fall_detection_dataset.zip` |
| `/content/drive/MyDrive/yolo_fastest_darknet_person/` | `dataset_cleaning.py`（リポジトリの `dataset/scripts/` から） |
| 同上 | `hard_negative_mining.py`（同上） |
| 同上 `backup/` | Phase 2 の `_best.weights` または `_final.weights`（finetune 起点） |

**⚠️ これらが無くてもノートブックは例外を投げない。** cell[10] は
`print('ERROR: dataset_cleaning.py が見つかりません')` を出して**クリーニングを飛ばし**、
cell[12] は `print('SKIP: Phase 2 weights が見つかりません')` を出して
**hard negative mining を飛ばした**まま学習に進む。
`output_type: "error"` にならないため、実行後のノートブック検査でも見逃しやすい。

そのため `check_notebook_errors.py` は例外だけでなく、出力テキスト中の
`ERROR:` / `SKIP:` / `WARNING:` も検出して終了コード 1 を返すようにしてある。**実行後は必ず通すこと。**

```bash
python3 dataset/scripts/colab_cli/check_notebook_errors.py train_yolo_fastest_darknet_colab_output.ipynb
```

#### (b) `--timeout` は学習時間を上回る値にする

cell[26] は darknet を起動したあと `for line in proc.stdout:` で**同期的にブロック**し、
学習が終わるまでセルが返らない。したがって `--timeout` は全体の所要時間を上回る必要がある。

| GPU | 100k iteration の目安（手順書 5.5 節） |
|-----|--------------------------------------|
| T4 | 3.5〜5 時間 |
| L4 / A100 | 1.5〜3 時間 |

```bash
colab --auth=adc exec -s trainer \
  -f dataset/scripts/train_yolo_fastest_darknet_colab.ipynb \
  --timeout 21600     # 6 時間。GPU に応じて余裕をもたせる
```

これは 6.2 で述べた「長時間ジョブはバックグラウンド化する」という方針とは逆になるが、
ノートブックの学習セルが同期実行である以上、この形を取らざるを得ない。

打ち切られた場合、**`_last.weights` が書かれていれば**続きから再開できる。cell[25] が `backup` を
Drive へのシンボリックリンクに置き換えるため checkpoint は Drive に残り、`.issue137_started`
マーカーと `_last.weights` の組で再開が成立する（これが元ノートブックの切断対策）。

> **⚠️ 最初の checkpoint が書かれる前に落ちた場合は再開できない。**
> cell[25] は**学習を始める前に** `.issue137_started` を作成し、Phase 2 weights の選択は
> `if not is_issue137_started:` の中でのみ行う。したがって初回が即時 OOM・cfg 不正・
> 早期切断などで `_last.weights` を残さずに終わると、再実行時は
> 「マーカーあり・`_last.weights` なし」となり、**Phase 2 起点が選ばれず COCO / backbone に
> フォールバックする**。エラーにはならないため、気づかないまま別条件の実験が走る。
>
> 再実行の前に必ず確認する。
>
> ```bash
> colab --auth=adc ls -s trainer /content/drive/MyDrive/yolo_fastest_darknet_person
> colab --auth=adc ls -s trainer /content/drive/MyDrive/yolo_fastest_darknet_person/backup
> ```
>
> `.issue137_started` があるのに `backup/` に `*_last.weights` が無ければ、マーカーを削除してから
> 再実行する（Phase 2 起点の初回実行として扱わせる）。
>
> ```bash
> colab --auth=adc rm -s trainer /content/drive/MyDrive/yolo_fastest_darknet_person/.issue137_started
> ```
>
> なお学習開始直後に `[Phase 3] Phase 2 weights を finetune 起点として使用` と表示されるか、
> それとも `[新規] backbone 重みで学習` / `[新規] COCO フル重みで学習` になっているかで、
> 起点が正しいかを判別できる。
>
> ノートブック側の恒久対策（checkpoint が存在してからマーカーを作る、あるいはマーカーがあって
> `_last.weights` が無ければ Phase 2 起点として扱う）は #148 の課題とする。

#### (c) 実行後の確認

**前処理セルはスクリプトの終了コードを捨てる。** cell[10] / cell[12] は
`subprocess.run(..., check=False)` で呼び出しているため、`dataset_cleaning.py` や
`hard_negative_mining.py` が異常終了しても学習に進む。
`check_notebook_errors.py` は子プロセスの `Traceback` までは拾えるが、
Traceback を出さずに終了した場合は検出できない。

そのため実行後に**成果物の側から**確認する。

```bash
# 1. ノートブックのセルエラー・ERROR:/SKIP:/Traceback を検査
python3 dataset/scripts/colab_cli/check_notebook_errors.py train_yolo_fastest_darknet_colab_output.ipynb

# 2. 前処理のレポートが今回の実行で更新されているか確認
colab --auth=adc ls -s trainer /content/drive/MyDrive/yolo_fastest_darknet_person
#    phase2_cleaning_report.json   … cell[10] が生成（クリーニング実施の証跡）
#    phase3_hardneg_report.json    … cell[12] が生成（hard negative mining 実施の証跡）
```

レポートが無い、または日時が古い場合は、その前処理が実行されていない。
学習をやり直すこと。

> ノートブックは `colab exec -f` で直接実行でき、`!` や `%%bash` のセルも動作する（6.4 参照）。

`MAX_BATCHES = None` にしても本番学習にはならない。cfg の `max_batches` がそのまま使われるだけで、
上表のセルは実行されない。

---

## 7. 実測値

| 項目 | 実測 |
|------|------|
| `/mnt/c` の読み出し | 129 MB/s |
| `colab upload` の実効速度 | 3.05 MB/s（50 MB: 16.5 秒 / 64 MB: 20.1 秒） |
| 895 MB の分割アップロード（64 MB × 14） | 293 秒、md5 一致 |
| データセット 4.87 GB の展開（Drive → VM） | 1 分 37 秒 |
| darknet ビルド（GPU=1 / CUDNN=1 / OPENCV=0） | 数分（バイナリ 2,408,832 バイト） |
| 学習速度（L4 / 192x192 / batch=64） | **約 0.09 秒/iteration** |
| 本番 100,000 iteration の見積もり | **約 2.5 時間** |

データセットの内訳（VM 上での展開結果。ローカル `dataset/merged/` と一致）:

```
images/train: 32,853   images/val: 2,368   images/test: 2,369
labels/train: 32,853   labels/val: 2,368   labels/test: 2,369
```

---

## 8. コマンド早見表

```bash
colab --auth=adc whoami                       # 認証状態（メール・スコープ・有効期限）
colab --auth=adc new -s <name> --gpu L4       # セッション作成（T4/L4/G4/H100/A100）
colab --auth=adc sessions                     # 一覧
colab --auth=adc status -s <name>             # ハードウェアと IDLE/BUSY
colab --auth=adc drivemount -s <name>         # Drive マウント（TTY 必須）
colab --auth=adc exec -s <name> -f x.py       # ローカルの .py / .ipynb を VM で実行
colab --auth=adc upload   -s <name> <local> <remote>
colab --auth=adc download -s <name> <remote> <local>
colab --auth=adc ls -s <name> <path>
colab --auth=adc log -s <name> -n 20          # セッションの操作履歴
colab --auth=adc stop -s <name>               # 停止（必須）
colab skill                                   # エージェント向けの同梱手順書
colab readme                                  # 同梱 README
```

> `colab repl` / `console` / `auth` / `drivemount` は TTY を要求する。
> エージェントから対話実行するとハングするため、人が手動で実行すること。

---

## 9. 参考

- https://github.com/googlecolab/google-colab-cli
- https://pypi.org/project/google-colab-cli/
- Issue #179（本ガイドの構築・検証記録。実行ログや切り分けの詳細はコメントを参照）
- Issue #148（本環境を用いるモデル改善タスク）
- `dataset/scripts/train_yolo_fastest_darknet_colab.ipynb`（元の学習ノートブック）
