"""バックグラウンドジョブの進捗を確認する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f poll.py --timeout 300

VM 側で WAIT_SEC 待ってからログ末尾を出す。ローカル端末は占有しない。
完了判定はプロセスの有無ではなく、ログ中の完了マーカーで行う
（darknet は終了処理中もしばらく pgrep に残るため）。
"""
import os
import subprocess
import time

# --- 設定 ---
WAIT_SEC = 60
TAIL_LINES = 20
LOG_CANDIDATES = ["/content/train.log", "/content/setup.log"]
DONE_MARKERS = ["SETUP DONE", "TRAIN DONE"]
BACKUP_DIR = "/content/backup_smoke"

time.sleep(WAIT_SEC)

# 存在するログのうち最も新しいものを見る
logs = [p for p in LOG_CANDIDATES if os.path.isfile(p)]
if not logs:
    print("ログが見つかりません:", LOG_CANDIDATES)
    raise SystemExit(0)
log = max(logs, key=os.path.getmtime)
print("log:", log)

r = subprocess.run(["pgrep", "-af", "darknet|setup.sh|train.sh"], capture_output=True, text=True)
print("process:", (r.stdout.strip().splitlines() or ["(not running)"])[0][:100])

with open(log) as f:
    lines = f.read().splitlines()

done = [m for m in DONE_MARKERS if any(m in ln for ln in lines)]
print("log lines:", len(lines))
print("完了マーカー:", done or "(まだ)")
print("--- tail ---")
print("\n".join(lines[-TAIL_LINES:]))

if os.path.isdir(BACKUP_DIR):
    print()
    print("===", BACKUP_DIR, "===")
    for n in sorted(os.listdir(BACKUP_DIR)):
        p = os.path.join(BACKUP_DIR, n)
        print(f"  {n}: {os.path.getsize(p):,} bytes")
