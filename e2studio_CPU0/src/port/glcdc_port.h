/**
 * @file glcdc_port.h
 * @brief GLCDC (Graphics LCD Controller) port layer for EK-RA8P1 board
 * @details
 * Provides GLCDC initialization, display control, and diagnostic functions
 * for the 1024x600 LCD panel on the EK-RA8P1 Parallel Graphics Expansion Board.
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
 * Fill a frame buffer with a solid color
 *
 * @details Fills the entire framebuffer with the specified RGB565 color value.
 *
 * @param p_fb     Pointer to the frame buffer
 * @param color565 RGB565 color value
 */
void glcdc_port_fill_color(uint8_t *p_fb, uint16_t color565);

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
 *            display status  - Show GLCDC timing parameters and configuration
 *            display fb      - Show frame buffer addresses and sizes
 *            display test    - Draw test patterns on the LCD
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
