/**
 * @file time_cmd.h
 * @brief NT-Shell "time" コマンドハンドラ宣言（S-012-2 / Issue #212）
 * @details
 * `usrcmd.c` の `cmdlist[]` へ登録するための宣言のみを持つ。
 * 実装の分離パターンは `fall_detection_cmd.h` / `ai_application/ai_cmd.h` に合わせている。
 */

#ifndef TIME_CMD_H
#define TIME_CMD_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * NT-Shell "time" コマンドハンドラ
 *
 * サブコマンド: （なし）= 現在時刻表示 / set / status
 *
 * @param argc 引数の個数
 * @param argv 引数文字列
 * @return `CMD_OK` または `cmd_utils.h` の `CMD_ERR_*`
 */
int usrcmd_time(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* TIME_CMD_H */
