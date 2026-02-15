#include "ntshell_thread.h"
#include "jlink_console.h"
#include "ntshell.h"
#include "usrcmd.h"

/* NT-Shell用コールバック関数（外部定義） */
extern int ntshell_serial_read(char *buf, int cnt, void *extobj);
extern int ntshell_serial_write(const char *buf, int cnt, void *extobj);
extern int ntshell_callback(const char *text, void *extobj);


/**
 * NT-Shell用シリアル読み込み関数
 * @param buf 読み込みバッファ
 * @param cnt 読み込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に読み込んだバイト数
 */
int ntshell_serial_read(char *buf, int cnt, void *extobj)
{
    (void)extobj;
    int i;

    for (i = 0; i < cnt; i++)
    {
        /* 1文字受信（ブロッキング） */
        buf[i] = input_from_console();
    }

    return i;
}

/**
 * NT-Shell用シリアル書き込み関数
 * @param buf 書き込みバッファ
 * @param cnt 書き込みバイト数
 * @param extobj 拡張オブジェクト（未使用）
 * @return 実際に書き込んだバイト数
 */
int ntshell_serial_write(const char *buf, int cnt, void *extobj)
{
    (void)extobj;

    /* 文字列をコンソールに出力 */
    /* 注意: print_to_consoleはNULL終端文字列を期待するため、
       バッファをコピーしてNULL終端を追加 */
    char temp[256];
    int len = (cnt < 255) ? cnt : 255;
    memcpy(temp, buf, len);
    temp[len] = '\0';

    print_to_console(temp);

    return len;
}


/**
 * コマンド処理コールバック
 * @param text 入力されたコマンド文字列
 * @param extobj 拡張オブジェクト（未使用）
 * @return 0: 成功
 */
int ntshell_callback(const char *text, void *extobj)
{
    (void)extobj;

    usrcmd_execute(text);

    return 0;
}

/* NT-Shell Thread entry function */
/* pvParameters contains TaskHandle_t */
void ntshell_thread_entry(void *pvParameters) {
	FSP_PARAMETER_NOT_USED(pvParameters);
	/* NT-Shellインスタンス */
	static ntshell_t ntshell;

    /* Initialise user level serial console support using SEGGER serial driver DEBUG1 */
    jlink_console_init ();

	/* TODO: add your own code here */
    /* NT-Shellの初期化 */
    ntshell_init(
        &ntshell,
        ntshell_serial_read,
        ntshell_serial_write,
        ntshell_callback,
        NULL    /* 拡張オブジェクト（未使用） */
    );

    /* プロンプトの設定 */
    ntshell_set_prompt(&ntshell, ">");

    while (1) {
        /* NT-Shell実行（この関数は戻らない） */
        ntshell_execute(&ntshell);
	}
}
