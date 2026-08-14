/**
 * @file hal_entry.c
 * @brief FSP ベアメタル構成のエントリ関数（本プロジェクトでは実行されない）
 * @details
 * Issue #186 Step 2 で FSP の RTOS 設定を FreeRTOS -> No RTOS へ変更したことにより、
 * `ra_gen/main.c`（編集禁止）が
 *
 *     int main(void) { hal_entry(); return 0; }
 *
 * という形で再生成された。`hal_entry()` が未定義だとリンクエラーになるため、
 * 本ファイルで定義する。RA Configuration エディタは本ファイルが存在しない場合のみ
 * テンプレートを生成するため、以降の Generate Project Content で上書きされない。
 *
 * **本関数は実行されない。** 本プロジェクトは方式A で μT-Kernel 3.0 を起動する:
 *
 *     R_BSP_WarmStart(BSP_WARM_START_POST_C)
 *       -> __init_array の mimamori_start_mtkernel()（src/hal_warmstart.c:161）
 *         -> knl_start_mtkernel()（戻らない ― src/hal_warmstart.c:165-166）
 *           -> μT-Kernel 初期タスク -> usermain()（src/usermain.c）
 *
 * したがって `main()` にも `hal_entry()` にも制御は渡らない。FSP テンプレートが
 * 生成する `R_BSP_SecondaryCoreStart()` の呼び出しは**意図的に削除**した ―
 * CPU1 の起動は usermain()（src/usermain.c:336）で実施済みであり、ここに
 * 二重に置くと「どちらが実際に CPU1 を起動するのか」を誤読させるため。
 *
 * 万一 μT-Kernel の起動を無効化（src/hal_warmstart.c の
 * MIMAMORI_USE_MTKERNEL_BOOT を 0）した場合に本関数へ到達しうるので、
 * その場合に気付けるようトラップして停止する。
 *
 * @see doc/migration/mtk3-migration-guide.md 10.3（Issue #186 Step 2）
 */

#include "hal_data.h"

/*******************************************************************************************************************//**
 * ra_gen/main.c の main() から呼ばれる（= 本プロジェクトでは到達しない）。
 **********************************************************************************************************************/
void hal_entry (void)
{
    /* 到達しない。到達した場合は μT-Kernel が起動していない異常状態なので停止する。 */
    while (1)
    {
        __asm volatile ("nop");
    }
}
