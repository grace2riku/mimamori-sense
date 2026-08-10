"""Drive 上の学習成果物に書き込みが発生していないことを、スナップショット比較で検証する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/drive_guard.py --timeout 300

使い方: **検証したい操作の前後で 2 回実行する**。

  1 回目: 基準となるスナップショット（パス・サイズ・mtime）を取得して保存する
  2 回目: 現在の状態とスナップショットを比較し、追加・削除・変更を報告する

  スナップショットは /content/.drive_guard_baseline.json に置くため、VM を止めると消える。
  同一セッション内での「この操作が Drive を書き換えていないか」の確認に使う。

★日付では判定しない
  以前は「本日更新されたファイルがあるか」で判定していたが、
    - 検証が日付をまたぐと、直前の書き込みが前日扱いになり見逃す
    - 同じ日に行われた無関係な更新を誤検出する
  という問題があった。基準スナップショットとの比較に変更している。

用途: 検証目的の学習を回したあとに実行し、#148 Phase 3 の重みが無傷であることを確認する。
      元ノートブックの cell[25] を誤って実行すると Drive の backup が書き換わるため、
      その検出用でもある。
"""
import datetime
import json
import os

TARGET = "/content/drive/MyDrive/yolo_fastest_darknet_person"
BASELINE = "/content/.drive_guard_baseline.json"


def snapshot(root):
    """root 以下の全ファイルの (サイズ, mtime) を集める。"""
    state = {}
    for dirpath, _dirs, files in os.walk(root):
        for name in files:
            p = os.path.join(dirpath, name)
            try:
                st = os.stat(p)
            except OSError:
                continue
            state[os.path.relpath(p, root)] = [st.st_size, round(st.st_mtime, 3)]
    return state


def fmt(entry):
    size, mtime = entry
    ts = datetime.datetime.fromtimestamp(mtime)
    return f"{size:,} bytes  {ts:%Y-%m-%d %H:%M:%S}"


# 対象ディレクトリがまだ無い場合も「空の状態」として基準に記録する。
# ここで基準を残さずに終了すると、検証対象の操作がディレクトリごと作成した場合に、
# 2 回目の実行がその状態を初期状態とみなしてしまい、検出したい書き込みを見逃す。
exists = os.path.isdir(TARGET)
current = snapshot(TARGET) if exists else {}

if not os.path.exists(BASELINE):
    with open(BASELINE, "w") as f:
        json.dump(current, f)
    print("=== 基準スナップショットを取得しました ===")
    print("  対象  :", TARGET)
    if not exists:
        print("  状態  : ディレクトリはまだ存在しない（空の基準として記録）")
    print("  ファイル数:", len(current))
    print("  保存先:", BASELINE)
    print()
    print("検証したい操作を実行したあと、本スクリプトをもう一度実行してください。")
    raise SystemExit(0)

with open(BASELINE) as f:
    base = json.load(f)

added = sorted(set(current) - set(base))
removed = sorted(set(base) - set(current))
changed = sorted(k for k in set(base) & set(current) if base[k] != current[k])

print("=== 基準スナップショットとの比較 ===")
print("  対象      :", TARGET)
if not exists:
    print("  状態      : ディレクトリが存在しない")
print("  基準       :", len(base), "ファイル")
print("  現在       :", len(current), "ファイル")
print()

if not (added or removed or changed):
    print("差分なし → Drive は無傷")
    raise SystemExit(0)

if added:
    print(f"★追加 {len(added)} 件:")
    for k in added[:10]:
        print(f"    {k}: {fmt(current[k])}")
if removed:
    print(f"★削除 {len(removed)} 件:")
    for k in removed[:10]:
        print(f"    {k}: (基準では {fmt(base[k])})")
if changed:
    print(f"★変更 {len(changed)} 件:")
    for k in changed[:10]:
        print(f"    {k}")
        print(f"      基準: {fmt(base[k])}")
        print(f"      現在: {fmt(current[k])}")

print()
print("Drive 上の成果物が書き換わっています。意図した書き込みか確認してください。")
raise SystemExit(1)
