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
- 既存のブランチ `feature/issue-$ARGUMENTS` があるか確認してください
  - 存在する場合は、そのブランチにマージされていないコミットがないか確認し、ユーザーに削除の確認を取ってから削除してください

### 2. Worktree 作成
- `git worktree add issue-$ARGUMENTS -b feature/issue-$ARGUMENTS` コマンドでWorktreeを作成してください
- Worktreeは `issue-$ARGUMENTS` という命名規則に従ったサブフォルダを作成します

### 3. Worktree 環境の設定
- 作成したサブディレクトリ `issue-$ARGUMENTS` に移動してください

### 4. 実装
- `gh issue view $ARGUMENTS` でIssue内容を確認し、最適なサブエージェントを選択して実装してください
- テスト可能な変更の場合は、実装完了後にテストを実行してください

### 5. プルリクエスト作成
- 変更をコミットし、リモートにプッシュしてください
- `gh pr create` コマンドでプルリクエストを作成してください
- PRタイトルはIssueの内容に応じたConventional Commits形式にしてください
  - 新機能: `feat: #$ARGUMENTS [要約]`
  - バグ修正: `fix: #$ARGUMENTS [要約]`
  - ドキュメント: `docs: #$ARGUMENTS [要約]`
  - リファクタリング: `refactor: #$ARGUMENTS [要約]`

### 6. 後処理 (クリーンアップ)
- プルリクエスト作成後、プロジェクトルートディレクトリに戻ってください
- `git worktree remove issue-$ARGUMENTS` でWorktreeを削除してください
- ステップ1で `git stash` を実行した場合は `git stash pop` で退避した変更を復元してください
- 作業が完了したことを報告してください
