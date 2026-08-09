"""Drive 上の学習成果物に書き込みが発生していないことを検証する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/drive_guard.py --timeout 300

検証目的の学習を回したあとに実行し、#148 Phase 3 の重みが無傷であることを確認する。
元ノートブックの cell[25] を誤って実行すると Drive の backup が書き換わるため、
その検出用でもある。
"""
import datetime
import os

TARGET = "/content/drive/MyDrive/yolo_fastest_darknet_person"

print("=== Drive バックアップディレクトリ ===")
if not os.path.isdir(TARGET):
    print("  存在しない（本セッションでは作成していない）")
    raise SystemExit(0)

for root, dirs, files in os.walk(TARGET):
    rel = os.path.relpath(root, TARGET)
    for n in sorted(files)[:20]:
        p = os.path.join(root, n)
        mt = datetime.datetime.fromtimestamp(os.path.getmtime(p))
        print(f"  {rel}/{n}: {os.path.getsize(p):,} bytes  更新 {mt:%Y-%m-%d %H:%M}")
    if root.count(os.sep) - TARGET.count(os.sep) > 1:
        dirs[:] = []

print()
print("=== 本日更新されたファイル ===")
today = datetime.date.today()
hits = []
for root, _dirs, files in os.walk(TARGET):
    for n in files:
        p = os.path.join(root, n)
        if datetime.date.fromtimestamp(os.path.getmtime(p)) == today:
            hits.append(p)

if hits:
    print(f"  ★{len(hits)} 件が本日更新されています。意図した書き込みか確認してください:")
    for p in hits[:10]:
        print("   ", p)
else:
    print("  0 件 → Drive は無傷")
