/**
 * @file lvgl_thread_entry.c
 * @brief LVGL thread entry point - display initialization and LVGL main loop
 * @details
 * This thread performs the LVGL and GLCDC display initialization sequence,
 * then runs the LVGL timer handler in a loop to process rendering and
 * input events.
 *
 * Initialization sequence:
 *   1. lv_init() - Initialize the LVGL library (also initializes Dave2D via
 *      lv_draw_dave2d_init() when LV_USE_DRAW_DAVE2D=1)
 *   2. dave2d_port_init() - Verify Dave2D initialization by LVGL (S-004-1)
 *   3. glcdc_port_init() - Initialize GLCDC display subsystem:
 *      a. LCD hardware reset (DISP_RESET pin)
 *      b. lvgl_port_mtk3_open() -> R_GLCDC_Open() + R_GLCDC_Start()
 *         (R-006: uT-Kernel replacement of RM_LVGL_PORT_Open)
 *      c. Register backlight enable callback (after first frame flush)
 *   4. lv_port_indev_init() - Initialize touch panel input device (F-001-5):
 *      a. Create LVGL pointer-type input device
 *      b. Open I2C bus/device for GT911 touch controller
 *      c. Enable external IRQ (channel 19) for touch events
 *   5. ui_main_screen_create() - Create and load the main screen (F-001-7):
 *      a. Status bar (40px) with status label, datetime, settings button
 *      b. Camera image area (1024x560) with RGB565 SDRAM buffer
 *      c. Color bar test pattern drawn as initial content
 *   6. lv_timer_handler() loop - Process LVGL rendering at 1ms intervals
 *
 * Dave2D-LVGL Integration (S-004-3):
 *   When LV_USE_DRAW_DAVE2D=1 (set in FSP lv_conf.h), lv_init() automatically:
 *     a. Calls lv_draw_dave2d_init() to create the Dave2D draw unit (ID=4)
 *     b. Initializes the D/AVE 2D hardware via d2_opendevice() + d2_inithw()
 *     c. Registers the evaluate callback (_dave2d_evaluate) that claims
 *        supported draw tasks (fill, border, line, arc, image, label, triangle)
 *     d. Creates a render thread for asynchronous Dave2D task execution
 *     e. Creates a mutex (xd2Semaphore) for thread-safe hardware access
 *   Unsupported operations (gradients, box shadows, masks) automatically fall
 *   back to the software renderer (LV_USE_DRAW_SW=1).
 *
 * Preconditions (satisfied before this task starts):
 *   - SDRAM initialized by R_BSP_SdramInit() in hal_warmstart.c
 *   - IOPORT opened by R_IOPORT_Open() in hal_warmstart.c
 *   - uT-Kernel 3.0 running (R-003 boot method A)
 *
 * R-006 / Issue #156 (FreeRTOS -> uT-Kernel 3.0 migration):
 *   The thread body was ported to the uT-Kernel task form
 *   lvgl_task(INT stacd, void *exinf), created and started from
 *   src/usermain.c (tk_cre_tsk + tk_sta_tsk, itskpri=14 / stksz=8192).
 *   - LVGL OSAL: LV_USE_OS = LV_OS_CUSTOM -> src/lv_os_mtkernel.c
 *     (lv_init() creates the dave2d/swdraw render threads through it)
 *   - Display port: RM_LVGL_PORT bypassed -> src/port/lvgl_port_mtk3.c
 *     (called from glcdc_port_init())
 *   - D/AVE 2D dlist sync: ra/fsp/src/r_drw/r_drw_irq.c excluded from build,
 *     replaced by src/port/r_drw_irq_mtk3.c
 *   - Main loop: vTaskDelay(1) -> tk_dly_tsk driven by the lv_timer_handler()
 *     return value (ms until the next timer)
 *   The old FreeRTOS entry lvgl_thread_entry() remains at the end of this
 *   file as a thin wrapper (link resolution for ra_gen/lvgl_thread.c - same
 *   pattern as ntshell/camera). Details: doc/migration/mtk3-migration-guide.md 7.4.
 *
 * Reference:
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c
 *   - GLCDC init: e2studio_CPU0/src/port/glcdc_port.c (glcdc_port_init)
 *   - Dave2D init: e2studio_CPU0/src/port/dave2d_port.c (dave2d_port_init)
 *   - Touch init: e2studio_CPU0/src/port/lv_port_indev.c (lv_port_indev_init)
 *   - Main screen: e2studio_CPU0/src/ui/ui_main_screen.c (ui_main_screen_create)
 *   - RM_LVGL_PORT: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c
 *   - LVGL Dave2D: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c
 *   - _dave2d_evaluate: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:238-367
 *
 * @note
 * This file is part of the GLCDC control (S-002-2), Dave2D control (S-004-1),
 * LVGL-Dave2D integration (S-004-3), touch panel control (F-001-5),
 * and main screen UI (F-001-7) implementation.
 */

#include "lvgl_thread.h"
#include "lvgl.h"
#include "port/glcdc_port.h"
#include "port/dave2d_port.h"
#include "port/lv_port_indev.h"
#include "ui/ui_main_screen.h"
#include "ui/fall_detection_screen.h"
#include "camera_display.h"

#include <tk/tkernel.h>

/**
 * LVGL task body (uT-Kernel)
 *
 * @details Initializes the LVGL library and GLCDC display, then enters
 *          the LVGL main loop. The loop calls lv_timer_handler() to
 *          process pending LVGL tasks (rendering, animations, timers)
 *          and sleeps with tk_dly_tsk() until the next LVGL timer is due.
 *
 * R-006: created and started from usermain() (tk_cre_tsk + tk_sta_tsk,
 *        itskpri=14 / stksz=8192 - same stack size as the FreeRTOS
 *        lvgl_thread in ra_gen/lvgl_thread.c).
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:16-78
 *
 * @param stacd  task start code (unused)
 * @param exinf  extended information (unused)
 */
void lvgl_task(INT stacd, void *exinf)
{
    (void)stacd;
    (void)exinf;

    /*
     * Step 1: Initialize LVGL library
     *
     * This must be called before any other LVGL function.
     * Initializes LVGL's internal data structures, memory management,
     * timer system, and default theme.
     *
     * When LV_USE_DRAW_DAVE2D=1 (FSP lv_conf.h:107), lv_init()
     * also calls lv_draw_dave2d_init() which:
     *   - Creates the Dave2D device handle via d2_opendevice(0)
     *   - Initializes Dave2D hardware via d2_inithw()
     *   - Registers the Dave2D draw unit (ID=4, name="DAVE2D")
     *   - Creates render buffers and the render thread
     *   - Sets up the evaluate callback for draw task dispatch
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:24
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
     */
    lv_init();

    /*
     * Step 2: Verify Dave2D initialization (S-004-1)
     *
     * dave2d_port_init() verifies that LVGL has successfully initialized
     * the Dave2D hardware by checking the internal handle (_d2_handle).
     * It does NOT duplicate the initialization.
     *
     * The status can be checked via the NT-Shell "dave2d status" command.
     *
     * Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
     */
    dave2d_port_init();

    /*
     * Step 4: Initialize GLCDC display subsystem
     *
     * This performs:
     *   a. LCD hardware reset (DISP_RESET pin pulse)
     *   b. lvgl_port_mtk3_open() (R-006: uT-Kernel replacement of
     *      RM_LVGL_PORT_Open - src/port/lvgl_port_mtk3.c):
     *      - R_GLCDC_Open() with a copy of g_display0_cfg (callback swapped)
     *      - R_GLCDC_Start() to begin display output
     *      - R_GLCDC_BufferChange() for double buffering setup
     *      - lv_display_create() and flush callback registration
     *   c. Backlight enable callback registration (fires after first flush)
     *
     * If initialization fails, the thread continues running but no
     * display output will be produced. The error can be checked via
     * the NT-Shell "display status" command.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:26
     */
    glcdc_port_init();

    /*
     * Step 5: Initialize touch panel input device (F-001-5)
     *
     * This initializes the GT911/FT5X06-compatible touch controller:
     *   a. Creates LVGL pointer-type input device
     *   b. Opens I2C bus and communication device (slave addr 0x38)
     *   c. Creates uT-Kernel event flag/semaphore for I2C and IRQ
     *      synchronization (R-006: bus switched to rm_comms_i2c callback mode)
     *   d. Opens and enables external IRQ (channel 19) for touch events
     *
     * Must be called after glcdc_port_init() because the LCD reset pin
     * is shared with the touch controller.
     *
     * Note: This requires FSP configuration for I2C and External IRQ
     * (Issue #3). Until the FSP modules are added, this call will
     * cause a build error.
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:27
     */
    lv_port_indev_init();

    /*
     * Step 6: UI initialization (initial screen display) (F-001-7)
     *
     * Creates and loads the main screen with:
     *   - Status bar (40px) at top: status label, datetime, settings button
     *   - Camera image area (1024x560) below: RGB565 buffer in SDRAM
     *   - Color bar test pattern displayed in camera area (until F-002
     *     provides actual camera frames)
     *
     * The screen is loaded as the active screen via lv_screen_load()
     * inside ui_main_screen_create().
     *
     * Reference: e2studio_CPU0/src/ui/ui_main_screen.c
     */
    ui_main_screen_create();

    /*
     * Step 7: Initialize camera display transfer (F-001-8)
     *
     * Creates an LVGL timer that periodically polls for new camera frames
     * from camera_framebuffer and transfers them to the LVGL camera image
     * buffer with letterbox centering (768x450 -> centered in 1024x560).
     *
     * The timer fires every 16ms (~60Hz) and calls camera_framebuffer_get_latest().
     * When a new frame is available, it is copied to the display buffer and
     * the LVGL image widget is invalidated.
     *
     * Depends on:
     *   - ui_main_screen_create() (display buffer must exist)
     *   - camera_framebuffer_init() (called by camera_thread, may not have
     *     executed yet; the timer gracefully handles NULL returns)
     *
     * Reference: e2studio_CPU0/src/camera_display.c
     */
    camera_display_init();

    /*
     * Step 7b: Initialize fall detection screen overlay (F-003-10)
     *
     * Creates LVGL overlay widgets (bounding boxes, status text, info panel)
     * on top of the camera image for displaying fall detection results.
     *
     * Must be called after ui_main_screen_create() and camera_display_init()
     * so that the camera image widget is properly positioned.
     *
     * The overlay is updated from the camera_display timer callback when
     * new AI inference results are available.
     *
     * Reference: e2studio_CPU0/src/ui/fall_detection_screen.c
     */
    {
        lv_obj_t *screen = lv_screen_active();
        if (screen != NULL)
        {
            fall_detection_screen_init(screen);
        }
    }

    /*
     * Step 8: LVGL main loop
     *
     * lv_timer_handler() processes all pending LVGL tasks:
     *   - Rendering invalidated areas to the active framebuffer
     *     (using Dave2D GPU for supported operations, SW for others)
     *   - Running animation timers
     *   - Processing input device events
     *   - Executing user-defined timers
     *
     * R-006: the former `lv_timer_handler(); vTaskDelay(1);` loop is
     * replaced by a delay driven by the lv_timer_handler() return value
     * (milliseconds until the next due timer). With the uT-Kernel timer
     * period CNF_TIMER_PERIOD=10 (ms), tk_dly_tsk is quantized to 10ms,
     * giving an effective refresh of ~50fps for LV_DEF_REFR_PERIOD=16ms
     * (KPI 30fps satisfied). Vsync / dlist-completion wake-ups are
     * event-driven (tk_sig_sem) and NOT quantized. If 60fps is required,
     * set CNF_TIMER_PERIOD=1 and re-measure (spike report 5.7 / 2.5).
     *
     * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/new_thread0_entry.c:60-77
     * Reference: doc/migration/r006a-lvgl-osal-spike.md 5.7
     */
    while (1) {
        uint32_t wait_ms = lv_timer_handler();  /* ms until the next timer */
        if (wait_ms == 0) {
            wait_ms = 1;            /* always yield at least 1ms */
        }
        if (wait_ms > 500) {
            wait_ms = 500;          /* clamp LV_NO_TIMER_READY etc. */
        }
        tk_dly_tsk((RELTIM)wait_ms);
    }
}

/**
 * 旧 FreeRTOS スレッドエントリ（R-006 移行前の名残り。対応関係追跡用に残置）。
 *
 * 方式A（src/hal_warmstart.c の静的コンストラクタで uT-Kernel を起動し、
 * ra_gen/main.c の main()/vTaskStartScheduler() に到達しない）では、本関数は
 * 実行時には呼ばれない。しかし ra_gen/lvgl_thread.c（編集禁止）の
 * lvgl_thread_func() が本シンボルを参照し、その参照鎖は startup が参照する
 * main() から辿れるためリンク時には解決が必要。よって削除せず、実体を
 * uT-Kernel タスク lvgl_task() へ委譲する薄いラッパとして残す
 * （実行されない。FreeRTOS ブートへ切り戻した場合もリンクのみ通る ― タスク本体が
 *   tk_* の uT-Kernel API を直接呼ぶため、実行には本体 API の FreeRTOS への差し戻しが必要）。
 * ntshell_thread_entry.c / camera_thread_entry.c 末尾のラッパと同一パターン。
 *
 * @param pvParameters FreeRTOSタスクパラメータ（未使用）
 */
void lvgl_thread_entry(void *pvParameters)
{
    FSP_PARAMETER_NOT_USED(pvParameters);
    /* uT-Kernel タスク本体へ委譲（stacd/exinf は未使用）。 */
    lvgl_task(0, NULL);
}
