"""バックグラウンドジョブの進捗を確認する。

実行場所: Colab VM
起動方法: colab --auth=adc exec -s <name> -f dataset/scripts/colab_cli/poll.py --timeout 300

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
FAIL_MARKERS = ["SETUP FAILED", "TRAIN FAILED"]
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
_running = bool(r.stdout.strip())
print("process:", (r.stdout.strip().splitlines() or ["(not running)"])[0][:100])

with open(log) as f:
    lines = f.read().splitlines()

done = [m for m in DONE_MARKERS if any(m in ln for ln in lines)]
failed = [ln for ln in lines if any(m in ln for m in FAIL_MARKERS)]
print("log lines:", len(lines))
if failed:
    print("★失敗:", failed[-1])
print("完了マーカー:", done or "(まだ)")

# 完了マーカーも失敗マーカーも無く、プロセスも居ない = SIGKILL や OOM などで
# マーカーを残せずに死んだ状態。ここを「実行中」と区別しないと、
# 手順どおりポーリングを繰り返しても永久に終わらない。
# 起動直後（プロセス生成前）を誤検出しないよう、ログの最終更新からの経過も見る。
if not done and not failed and not _running:
    _age = time.time() - os.path.getmtime(log)
    if _age > 30:
        print()
        print("★異常終了の可能性: 完了マーカーが無く、プロセスも見つかりません")
        print(f"  ログの最終更新から {_age:.0f} 秒経過しています。")
        print("  SIGKILL・OOM・VM の再起動などで強制終了した可能性があります。")
        print("  上のログ末尾を確認し、必要なら該当の起動スクリプトから再実行してください。")
    else:
        print("  （プロセス未検出だがログが新しいため、起動直後の可能性があります）")
print("--- tail ---")
print("\n".join(lines[-TAIL_LINES:]))

if os.path.isdir(BACKUP_DIR):
    print()
    print("===", BACKUP_DIR, "===")
    for n in sorted(os.listdir(BACKUP_DIR)):
        p = os.path.join(BACKUP_DIR, n)
        print(f"  {n}: {os.path.getsize(p):,} bytes")
