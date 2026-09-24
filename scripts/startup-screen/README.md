# 起動画面の画像変換・検証

リポジトリのルートから実行する。Python 3.10以降とPillowを使用する。

```powershell
python scripts/startup-screen/convert_image.py
python scripts/startup-screen/convert_image.py --check
```

変換先は `e2studio_CPU0/src/ui/ui_startup_image_data.inc`。原画の縦横比を保って
1024×600に収め、RGB565化した画素列をzlibで圧縮する。

## ファームウェア

通常はe2 studioでCPU0をリフレッシュしてビルドする（新しい `src/ui` 以下のCソースも対象）。
既存のDebug応答ファイルがある場合は、以下で全ソースを独立フォルダへ再ビルドできる。
出力名は未使用のものを指定する。既存成果物は上書きしない。

```powershell
python scripts/startup-screen/build.py --output e2studio_CPU0/Debug/startup-screen/build-new
```

LLVMの場所は `build.py` の `LLVM`。CPU0のELF、map、S-record、入力ハッシュ、ログを保存する。
CPU1の変更やボードへの書込みは行わない。

## 自動テスト

テスト用パッケージはファームウェアに組み込まれない。

```powershell
python -m pip install --target e2studio_CPU0/Debug/startup-screen/test-deps unicorn==2.1.4 pyelftools==0.32
python scripts/startup-screen/test_decode.py
python scripts/startup-screen/test_timing.py
```

- `test_decode.py`: 本番Cのzlibヘッダ／サイズ／チェックサム検証とpuffをArm命令にコンパイルして実行。画像全バイトの一致、破損・切断・出力不足、canaryを検証する。
- `test_timing.py`: 本番起動画面と実際のLVGLを実行。描画完了イベントを注入して5000ms境界等を検証し、最後にソフトウェア描画プレビューを生成する。

Unicornで実行できるCortex-M4命令・ハードウェアなしのテスト設定を使用する。
製品のM85/Dave2D設定はCPU0全体のビルドで別途検証する。
設計・検証結果・実機で必要な確認は `doc/design/startup-screen.md` を参照。
