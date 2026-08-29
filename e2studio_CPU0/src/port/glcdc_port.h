/**
 * @file glcdc_port.h
 * @brief GLCDC (Graphics LCD Controller) port layer for EK-RA8P1 board
 * @details
 * Provides GLCDC initialization, display control, double-buffering management,
 * and diagnostic functions for the 1024x600 LCD panel on the EK-RA8P1
 * Parallel Graphics Expansion Board.
 *
 * GLCDC Configuration Summary:
 *   Resolution       : 1024 x 600
 *   Input format     : RGB565 (16-bit)
 *   Output format    : RGB888 (24-bit)
 *   Pixel clock      : 50 MHz (LCDCLK 200MHz / 4)
 *   Frame rate       : ~59 Hz
 *   Frame buffers    : 2 (double buffering in SDRAM, .sdram_noinit_nocache)
 *
 * Clock chain:
 *   XTAL 24MHz -> PLL1 /3 x250 = 2GHz -> PLL1R /5 = 400MHz
 *   -> LCDCLK /2 = 200MHz -> GLCDC Panel Clock /4 = 50MHz
 *
 * Double Buffering Architecture (S-002-3):
 *   The double-buffering mechanism is implemented cooperatively between
 *   RM_LVGL_PORT (FSP middleware) and this port layer:
 *
 *   1. RM_LVGL_PORT_Open() configures LVGL with two framebuffers:
 *      - fb_background[0] and fb_background[1] in SDRAM (.sdram_noinit_nocache)
 *      - LVGL render mode: LV_DISPLAY_RENDER_MODE_DIRECT
 *      - Initially displays fb_background[1], LVGL renders to fb_background[0]
 *
 *   2. Vsync-synchronized buffer swap flow:
 *      a. LVGL renders dirty areas to the back buffer (current render target)
 *      b. rm_lvgl_port_flush_cb() calls R_GLCDC_BufferChange() to request
 *         the GLCDC hardware to switch to the newly rendered buffer
 *      c. rm_lvgl_port_flush_wait_cb() blocks on a FreeRTOS semaphore
 *         (g_semaphore_vpos) until the Vsync interrupt fires
 *      d. _rm_lvgl_port_display_callback() releases the semaphore on
 *         DISPLAY_EVENT_LINE_DETECTION (Vsync)
 *      e. LVGL can now safely render to the other buffer (the old front buffer)
 *
 *   3. This port layer tracks:
 *      - Buffer swap count (number of completed Vsync-synchronized swaps)
 *      - Current front/back buffer indices
 *      - Buffer swap timing (overhead measurement)
 *
 *   Reference: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:223-269
 *
 * Initialization Sequence (S-002-2):
 *   1. LCD hardware reset via DISP_RESET pin (shared with GT911 touch)
 *   2. RM_LVGL_PORT_Open() which internally calls:
 *      a. R_GLCDC_Open()  - GLCDC register configuration
 *      b. R_GLCDC_Start() - Start display output
 *      c. R_GLCDC_BufferChange() - Set initial framebuffer for double buffering
 *      d. lv_display_create() - Create LVGL display object
 *   3. Backlight enable via DISP_BLEN pin
 *   4. Vsync (line detection) interrupt handles buffer swap synchronization
 *
 * LCD Control Pins:
 *   DISP_RESET : P606 (BSP_IO_PORT_06_PIN_06) - LCD & touch controller reset
 *   DISP_BLEN  : P514 (BSP_IO_PORT_05_PIN_14) - Backlight enable
 *
 * Reference:
 *   - GLCDC configuration: e2studio_CPU0/ra_gen/common_data.c (g_display0_cfg)
 *   - Clock config: e2studio_CPU0/ra_gen/bsp_clock_cfg.h (BSP_CFG_LCDCLK_*)
 *   - Reference project: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c
 *   - Reference init: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:66-86
 *   - Reference reset: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:88-102
 *   - RM_LVGL_PORT_Open: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:82-171
 *   - Double buffering: e2studio_CPU0/ra/fsp/src/rm_lvgl_port/rm_lvgl_port.c:223-269
 *   - Timing design: doc/design/glcdc-timing-parameters.md
 *
 * @note
 * This file is part of the GLCDC control (S-002-1, S-002-2, S-002-3, S-002-4)
 * implementation.
 */

#ifndef GLCDC_PORT_H
#define GLCDC_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include "rm_lvgl_port.h"
#include "common_data.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**
 * GLCDC display resolution
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.h:61-62
 *   DISPLAY_HSIZE_INPUT0 = 1024
 *   DISPLAY_VSIZE_INPUT0 = 600
 */
#define GLCDC_DISPLAY_HSIZE         (1024)
#define GLCDC_DISPLAY_VSIZE         (600)

/**
 * GLCDC timing parameters
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:163-170
 *   .htiming = { .total_cyc = 1344, .display_cyc = 1024,
 *                .back_porch = 160, .sync_width = 4 }
 *   .vtiming = { .total_cyc = 635, .display_cyc = 600,
 *                .back_porch = 23, .sync_width = 3 }
 */
#define GLCDC_HTIMING_TOTAL         (1344)
#define GLCDC_HTIMING_DISPLAY       (1024)
#define GLCDC_HTIMING_BACK_PORCH    (160)
#define GLCDC_HTIMING_SYNC_WIDTH    (4)
#define GLCDC_HTIMING_FRONT_PORCH   (GLCDC_HTIMING_TOTAL - GLCDC_HTIMING_DISPLAY \
                                     - GLCDC_HTIMING_BACK_PORCH - GLCDC_HTIMING_SYNC_WIDTH)

#define GLCDC_VTIMING_TOTAL         (635)
#define GLCDC_VTIMING_DISPLAY       (600)
#define GLCDC_VTIMING_BACK_PORCH    (23)
#define GLCDC_VTIMING_SYNC_WIDTH    (3)
#define GLCDC_VTIMING_FRONT_PORCH   (GLCDC_VTIMING_TOTAL - GLCDC_VTIMING_DISPLAY \
                                     - GLCDC_VTIMING_BACK_PORCH - GLCDC_VTIMING_SYNC_WIDTH)

/**
 * GLCDC clock parameters
 *
 * Reference: e2studio_CPU0/ra_gen/bsp_clock_cfg.h:57-58
 *   BSP_CFG_LCDCLK_SOURCE = PLL1R
 *   BSP_CFG_LCDCLK_DIV    = /2
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:103
 *   .clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_4
 */
#define GLCDC_LCDCLK_HZ            (200000000UL)   /* LCDCLK = PLL1R(400MHz) / 2 = 200MHz */
#define GLCDC_PANEL_CLK_DIV         (4)             /* GLCDC_PANEL_CLK_DIVISOR_4 */
#define GLCDC_PIXEL_CLOCK_HZ        (GLCDC_LCDCLK_HZ / GLCDC_PANEL_CLK_DIV)  /* 50 MHz */

/**
 * Frame buffer parameters
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.h:63-64
 */
#define GLCDC_FB_BPP                (16)            /* RGB565: 16 bits per pixel */
#define GLCDC_FB_COUNT              (2)             /* Double buffering */

/**
 * LCD control pin definitions
 *
 * These correspond to the symbolic pin names defined in bsp_pin_cfg.h.
 * The reference project uses "LCD_BLEN" / "LCD_RESET" but the current
 * project's FSP configuration uses "DISP_BLEN" / "DISP_RESET".
 *
 * Reference: e2studio_CPU0/ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h:95,103
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/ra_cfg/fsp_cfg/bsp/bsp_pin_cfg.h:95,103
 */
#define GLCDC_PIN_BACKLIGHT         DISP_BLEN       /* P514: Backlight enable */
#define GLCDC_PIN_RESET             DISP_RESET      /* P606: LCD & touch controller reset */

/**
 * Frame buffer size in bytes (single frame)
 *
 * Uses the FSP-calculated stride (aligned to 64-byte boundary) rather
 * than a simple width * bpp calculation.
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.h:63
 *   DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 = ((1024 * 16 + 0x1FF) >> 9) << 6 = 2048
 */
#define GLCDC_FB_SIZE_BYTES         (DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0)

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** GLCDC initialization status */
typedef enum {
    GLCDC_STATUS_NOT_INITIALIZED = 0,   /**< Not yet initialized */
    GLCDC_STATUS_INITIALIZED,           /**< Successfully initialized and running */
    GLCDC_STATUS_ERROR,                 /**< Initialization failed */
} glcdc_status_t;

/**
 * GLCDC timing information structure
 *
 * Holds horizontal and vertical timing parameters for diagnostic display.
 */
typedef struct {
    /* Horizontal timing */
    uint16_t h_total;           /**< Total horizontal cycles */
    uint16_t h_display;         /**< Horizontal display cycles (active area) */
    uint16_t h_back_porch;      /**< Horizontal back porch */
    uint16_t h_front_porch;     /**< Horizontal front porch */
    uint16_t h_sync_width;      /**< Horizontal sync pulse width */

    /* Vertical timing */
    uint16_t v_total;           /**< Total vertical lines */
    uint16_t v_display;         /**< Vertical display lines (active area) */
    uint16_t v_back_porch;      /**< Vertical back porch */
    uint16_t v_front_porch;     /**< Vertical front porch */
    uint16_t v_sync_width;      /**< Vertical sync pulse width */

    /* Clock */
    uint32_t pixel_clock_hz;    /**< Pixel clock frequency in Hz */
    uint32_t frame_rate_x100;   /**< Frame rate x100 (e.g., 5859 = 58.59 Hz) */
} glcdc_timing_info_t;

/**
 * GLCDC frame buffer information structure
 */
typedef struct {
    uint32_t fb0_addr;          /**< Frame buffer 0 address */
    uint32_t fb1_addr;          /**< Frame buffer 1 address */
    uint32_t fb_size_bytes;     /**< Size of one frame buffer in bytes */
    uint32_t stride_bytes;      /**< Line stride in bytes */
    uint32_t stride_pixels;     /**< Line stride in pixels */
    uint16_t bpp;               /**< Bits per pixel */
    uint16_t fb_count;          /**< Number of frame buffers */
} glcdc_fb_info_t;

/**
 * Double-buffering status information structure (S-002-3, revised by Issue #218)
 *
 * Tracks the state of the Vsync-synchronized double-buffering mechanism.
 * The buffer swap is managed cooperatively by the display port and this layer:
 *   - lvgl_port_mtk3_flush_cb() performs the actual R_GLCDC_BufferChange()
 *     and reports the result through glcdc_port_notify_flush()
 *   - This port layer counts flushes, tracks the last requested buffer
 *     address, and counts BufferChange failures
 *
 * Issue #218: the previous version derived swap_count and front_buffer_index
 * from the Vsync interrupt alone. Both were incremented/toggled on EVERY
 * Vsync regardless of whether LVGL had flushed anything, so "Swap Count" was
 * an alias of "Vsync Count" and "front buffer" was a free-running toggle
 * rather than the buffer the GLCDC actually reads. Neither could show that
 * LVGL had stopped flushing. They are replaced by the flush_* fields below,
 * which are written by the LVGL task from the flush callback.
 *
 * Reference: e2studio_CPU0/src/port/lvgl_port_mtk3.c (lvgl_port_mtk3_flush_cb)
 */
typedef struct {
    uint32_t flush_count;           /**< Number of completed LVGL flushes (buffer changes requested) */
    uint32_t vsync_count;           /**< Total Vsync interrupt count since initialization */
    uint32_t last_flush_addr;       /**< Buffer address passed to the last R_GLCDC_BufferChange() (0 = none yet) */
    uint32_t bufchange_err_count;   /**< Number of R_GLCDC_BufferChange() calls that returned an error */
    int32_t  bufchange_last_err;    /**< fsp_err_t returned by the last failing R_GLCDC_BufferChange() */
    uint32_t underflow_count;       /**< Number of GLCDC underflow errors detected */
    bool     double_buffer_enabled; /**< true if double buffering is active (2 framebuffers) */
} glcdc_dbuf_status_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initialize the GLCDC display subsystem
 *
 * @details Performs the full GLCDC initialization sequence:
 *   1. LCD hardware reset via DISP_RESET pin (high-low-high pulse)
 *   2. RM_LVGL_PORT_Open() - Opens GLCDC, starts display output,
 *      sets up double buffering, and creates LVGL display object
 *   3. Registers a one-shot LVGL event callback to enable the backlight
 *      after the first frame is flushed (avoids displaying garbage)
 *
 * This function must be called from the LVGL thread after lv_init().
 *
 * Preconditions:
 *   - SDRAM must be initialized (S-001-2: sdram_port_init() called)
 *   - lv_init() must have been called
 *   - IOPORT must be open (done in hal_warmstart.c BSP_WARM_START_POST_C)
 *
 * Reference: reference_projects/lv_port_renesas_ek_ra8p1/src/port/lv_port_disp.c:66-86
 *
 * @retval true   GLCDC initialized successfully
 * @retval false  GLCDC initialization failed
 */
bool glcdc_port_init(void);

/**
 * Get the current GLCDC initialization status
 *
 * @return Current GLCDC status
 */
glcdc_status_t glcdc_port_get_status(void);

/**
 * Check if GLCDC display is available for use
 *
 * @retval true   GLCDC is initialized and display is active
 * @retval false  GLCDC is not available
 */
bool glcdc_port_is_available(void);

/**
 * Get the Vsync interrupt count
 *
 * @details Returns the number of VSYNC (line detection) interrupts that
 *          have occurred since initialization. Useful for verifying that
 *          Vsync interrupts are firing correctly.
 *
 * @return Number of Vsync interrupts since initialization
 */
uint32_t glcdc_port_get_vsync_count(void);

/*
 * Issue #183 / #218: the test pattern generators (S-002-4) used to be excluded
 * from the default build together with the "display test" command. Issue #218
 * needs "display test colorbar" to write the GLCDC framebuffer WITHOUT going
 * through LVGL, which is the only way to tell a broken GLCDC/panel output path
 * apart from LVGL rendering white. Measured cost of building them in: 3,072
 * bytes of internal flash (.flash.endof 0x020C5400 -> 0x020C6000), so they are
 * now always built. See src/diag_config.h.
 */

/**
 * Draw a color bar test pattern to a frame buffer
 *
 * @details Fills the specified framebuffer with vertical color bars:
 *          Red, Green, Blue, Yellow, Cyan, Magenta, White, Black
 *          Each bar is (display_width / 8) pixels wide.
 *
 * @param p_fb  Pointer to the frame buffer (RGB565 format)
 */
void glcdc_port_draw_colorbar(uint8_t *p_fb);

/**
 * Draw a gradient test pattern to a frame buffer
 *
 * @details Fills the specified framebuffer with a horizontal gradient
 *          from black (left) to white (right) on the top half, and
 *          a red-to-blue gradient on the bottom half.
 *
 * @param p_fb  Pointer to the frame buffer (RGB565 format)
 */
void glcdc_port_draw_gradient(uint8_t *p_fb);

/**
 * Draw a checkerboard test pattern to a frame buffer
 *
 * @details Fills the specified framebuffer with a checkerboard pattern
 *          alternating between white and black squares. The pattern uses
 *          a configurable block size (default: 32x32 pixels) for visibility,
 *          and a 1-pixel variant for verifying pixel-level rendering accuracy.
 *
 *          This pattern is useful for verifying:
 *            - Correct pixel addressing (no off-by-one errors)
 *            - GLCDC stride/alignment correctness
 *            - Display timing (misalignment causes visible diagonal distortion)
 *
 * @param p_fb       Pointer to the frame buffer (RGB565 format)
 * @param block_size Size of each checker square in pixels (e.g., 1, 8, 32)
 */
void glcdc_port_draw_checker(uint8_t *p_fb, uint32_t block_size);

/**
 * Fill a frame buffer with a solid color
 *
 * @details Fills the entire framebuffer with the specified RGB565 color value.
 *
 * @param p_fb     Pointer to the frame buffer
 * @param color565 RGB565 color value
 */
void glcdc_port_fill_color(uint8_t *p_fb, uint16_t color565);

/**
 * Control the LCD backlight
 *
 * @details Enables or disables the LCD backlight via the DISP_BLEN pin.
 *          This allows screen ON/OFF control from NT-Shell commands.
 *
 * @param enable  true to turn on the backlight, false to turn off
 */
void glcdc_port_backlight_control(bool enable);

/**
 * Get GLCDC timing information
 *
 * @details Fills the timing info structure with the configured GLCDC
 *          horizontal/vertical timing parameters and calculated pixel clock
 *          and frame rate values.
 *
 * @param info Pointer to glcdc_timing_info_t structure to fill
 */
void glcdc_port_get_timing_info(glcdc_timing_info_t *info);

/**
 * Get GLCDC frame buffer information
 *
 * @details Fills the frame buffer info structure with addresses, sizes,
 *          and format information for the GLCDC frame buffers.
 *
 * @param info Pointer to glcdc_fb_info_t structure to fill
 */
void glcdc_port_get_fb_info(glcdc_fb_info_t *info);

/**
 * Get double-buffering status information (S-002-3, revised by Issue #218)
 *
 * @details Fills the double-buffering status structure with the current
 *          state: LVGL flush count, Vsync count, the buffer address passed to
 *          the last R_GLCDC_BufferChange(), the BufferChange failure count and
 *          last error, the underflow count, and the double-buffering flag.
 *
 *          flush_count vs vsync_count: flush_count only advances when LVGL
 *          actually finished a frame and requested a buffer change, so a
 *          stalled flush_count with a still-advancing vsync_count means LVGL
 *          stopped rendering while the GLCDC keeps scanning out (Issue #218).
 *
 *          The buffer the GLCDC really reads is NOT tracked here - it is a
 *          hardware register (R_GLCDC->GR[0].FLM2) and is reported by the
 *          "display reg" sub-command.
 *
 * Reference: e2studio_CPU0/src/port/lvgl_port_mtk3.c (lvgl_port_mtk3_flush_cb)
 *
 * @param status Pointer to glcdc_dbuf_status_t structure to fill
 */
void glcdc_port_get_dbuf_status(glcdc_dbuf_status_t *status);

/**
 * Get the current GLCDC underflow error count (S-002-3)
 *
 * @details Returns the number of GLCDC underflow errors detected since
 *          initialization. An underflow occurs when the GLCDC cannot read
 *          framebuffer data from SDRAM fast enough, typically indicating
 *          SDRAM bandwidth contention.
 *
 * @return Number of underflow errors since initialization
 */
uint32_t glcdc_port_get_underflow_count(void);

/**
 * Get the LVGL flush count (S-002-3, revised by Issue #218)
 *
 * @details Returns the number of completed LVGL flushes since initialization.
 *          One flush = one call of the LVGL flush callback for the last area
 *          of a frame, i.e. one R_GLCDC_BufferChange() request.
 *
 * @return Number of LVGL flushes completed
 */
uint32_t glcdc_port_get_flush_count(void);

/**
 * Query the diagnostic blank request (Issue #218)
 *
 * @details Returns true while "display blank on" is in effect. The LVGL flush
 *          callback reads this once per rendered frame and, when set, hands
 *          NULL to R_GLCDC_BufferChange() instead of the rendered buffer. The
 *          FSP driver then makes the graphics plane transparent and disables
 *          its frame buffer read (AB1.DISPSEL = 1, FLMRD = 0;
 *          ra/fsp/src/r_glcdc/r_glcdc.c:666-685), so the panel shows the
 *          background plane colour BG.BGC with no SDRAM access at all.
 *
 *          This is a declaration/reconcile pair rather than a direct register
 *          write: R_GLCDC_BufferChange() must have no caller that can run
 *          concurrently with the flush callback, or its four register writes
 *          can interleave and latch "layer visible + FLM2 = 0". See CLAUDE.md,
 *          and doc/design/issue-218.md section 8.
 *
 *          Side effect (measured): FLMRD = 0 starves the layer-1 FIFO while
 *          the plane pipeline keeps running, so layer 1 underflows every
 *          frame while the blank is on. GR0.MON.UNDFLST latches and
 *          glcdc_underflow_1_isr() fires, advancing the underflow counter
 *          reported by "display dbuf". Neither indicates a real fault; the
 *          command says so in its output.
 *
 * Execution context: called from the LVGL task (flush callback). Never
 * blocks - it is a single volatile read.
 *
 * @retval true   The graphics plane should be hidden (diagnostic blank on)
 * @retval false  Normal operation
 */
bool glcdc_port_blank_requested(void);

/**
 * Report the result of one LVGL flush to the port layer (Issue #218)
 *
 * @details Called by lvgl_port_mtk3_flush_cb() immediately after its
 *          R_GLCDC_BufferChange() retry loop returns. Records the flush,
 *          the buffer address that was requested, and any error the FSP
 *          driver returned (which the flush callback itself discards).
 *
 * Execution context: LVGL task only. The flush callback is reached through
 * lv_timer_handler() -> lv_display_refr_timer() -> draw_buf_flush()
 * (ra/lvgl/lvgl/src/core/lv_refr.c:1348,1373,1409); there is no ISR path.
 * The updated variables are plain aligned 32-bit volatile stores from that
 * single writer, read by ntshell_task, so no critical section is needed.
 *
 * This function must not block: it runs on every rendered frame, immediately
 * before the Vsync wait.
 *
 * A NULL p_framebuffer is how the callback reports that it honoured a
 * "display blank" request, which is what acknowledges it to the shell. The
 * acknowledgement is conditional on err == FSP_SUCCESS: a BufferChange that
 * failed never reached the registers, so it must not be reported as applied.
 *
 * @param p_framebuffer Buffer address passed to R_GLCDC_BufferChange()
 *                      (NULL while the diagnostic blank is applied)
 * @param err           Value returned by R_GLCDC_BufferChange() (fsp_err_t)
 */
void glcdc_port_notify_flush(const void *p_framebuffer, int32_t err);

/**
 * LVGL GLCDC callback function
 *
 * @details Called from RM_LVGL_PORT module's display callback.
 *          Handles underflow error detection and Vsync counting.
 *          This function name is referenced in the FSP-generated
 *          common_data.c (g_lvgl_port_cfg.p_callback).
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:333
 *
 * @param p_arg  Callback arguments from RM_LVGL_PORT
 */
void lvgl_glcdc_callback(rm_lvgl_port_callback_args_t *p_arg);

/**
 * NT-Shell command handler for GLCDC display control
 *
 * @details Registered as the "display" command in usrcmd.c.
 *          Sub-commands:
 *            display status    - Show GLCDC timing parameters and configuration
 *            display fb        - Show frame buffer addresses and sizes
 *            display dbuf      - Show double-buffering status (S-002-3)
 *            display reg       - Read back GLCDC hardware registers (#218)
 *            display fbstat    - Sample the framebuffer contents (#218)
 *            display test      - Draw test patterns on the LCD (S-002-4)
 *            display backlight - Control LCD backlight on/off (S-002-4)
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_display(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* GLCDC_PORT_H */
