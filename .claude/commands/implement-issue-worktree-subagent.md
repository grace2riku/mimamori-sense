---
description: GitHub IssueをGit Worktreeで実装しPRを作成
argument-hint: <Issue番号>
---

Issue #$ARGUMENTS を Git Worktree を使って実装してください。

## 実装手順

### 1. 前処理 (Worktree 作成前)
- 未コミットの変更がないか `git status` で確認してください
  - 未コミットの変更がある場合は `git stash` で退避してください
- 現在のブランチがmain以外の場合は、mainブランチにチェックアウトしてください
- `git checkout main && git pull origin main` で最新のmainブランチを取得してください
  - 失敗した場合はエラー内容をユーザーに報告し、処理を中断してください
- 既存のブランチ `feature/issue-$ARGUMENTS` があるか確認してください
  - 存在する場合は、そのブランチにマージされていないコミットがないか確認し、ユーザーに削除の確認を取ってから削除してください
- 既存のWorktreeディレクトリ `issue-$ARGUMENTS` が残っていないか確認してください
  - 残っている場合は `git worktree remove issue-$ARGUMENTS` で削除してください

### 2. Worktree 作成・移動
- `git worktree add issue-$ARGUMENTS -b feature/issue-$ARGUMENTS` コマンドでWorktreeを作成してください
- Worktreeは `issue-$ARGUMENTS` という命名規則に従ったサブフォルダを作成します
- 作成したサブディレクトリ `issue-$ARGUMENTS` に移動してください

### 3. 実装
- `gh issue view $ARGUMENTS` でIssue内容を確認してください
  - 失敗した場合（Issue番号の誤り等）はユーザーに報告し、処理を中断してください
- Issue内容に基づき、以下から最適なサブエージェントを選択して実装してください:
  - **FSP設定が必要なIssue** → `.claude/agents/fsp-config-guide.md` を使用
    - 対象: Issueタイトルに「FSPプロジェクト設定」を含むもの
    - configuration.xmlの変更手順書を生成し、ユーザーに手動操作を依頼する
  - **リファレンスからのコード移植** → `.claude/agents/port-reference.md` を使用
    - 対象: ドライバ実装、スレッド実装、フレームバッファ管理、シェルコマンド追加
  - **LVGL設定・UI実装** → `.claude/agents/lvgl-ui.md` を使用
    - 対象: LVGL設定ファイル、画面レイアウト、ウィジェット、描画機能
- テスト可能な変更の場合は、実装完了後にテストを実行してください
- **以下のファイルは絶対に編集しないでください:**
  - `configuration.xml`（FSP設定ファイル）
  - `ra_gen/` 配下の自動生成コード
  - `ra/fsp/` 配下のFSPライブラリ
- Issueの実装にFSPの設定変更が必要な場合は、自分で編集せずユーザーに必要な変更内容を具体的に伝え、手動での変更を促してください

### 4. 実機動作確認（ユーザー確認）
- 実装完了後、変更内容のサマリと変更ファイル一覧をユーザーに提示してください
- ユーザーが評価ボード（EK-RA8P1）への書き込みと実機動作確認を行います
- **ユーザーから動作確認完了の通知があるまで次のステップに進まないでください**
- ユーザーから修正指示があった場合はステップ3に戻って対応してください

### 5. プルリクエスト作成
- 変更をコミットし、リモートにプッシュしてください
- `gh pr create --base main` コマンドでプルリクエストを作成してください
- PRタイトルはIssueの内容に応じたConventional Commits形式にしてください
  - 新機能: `feat: #$ARGUMENTS [要約]`
  - バグ修正: `fix: #$ARGUMENTS [要約]`
  - ドキュメント: `docs: #$ARGUMENTS [要約]`
  - リファクタリング: `refactor: #$ARGUMENTS [要約]`
- PRのbodyに `Closes #$ARGUMENTS` を含めてください（マージ時にIssueが自動クローズされます）

### 6. 後処理 (クリーンアップ)
- プルリクエスト作成後、プロジェクトルートディレクトリに戻ってください
- `git worktree remove issue-$ARGUMENTS` でWorktreeを削除してください
- ステップ1で `git stash` を実行した場合は `git stash pop` で退避した変更を復元してください
- 作業が完了したことを報告してください
