/**
 * @file dave2d_port.h
 * @brief Dave2D (D/AVE 2D) graphics accelerator port layer for EK-RA8P1 board
 * @details
 * Provides Dave2D initialization tracking, status query, and diagnostic
 * functions for the Renesas D/AVE 2D hardware graphics engine on the
 * RA8P1 MCU.
 *
 * Dave2D Initialization Architecture:
 *   The Dave2D hardware is initialized by LVGL's internal Dave2D draw unit
 *   (lv_draw_dave2d.c) when LV_USE_DRAW_DAVE2D is enabled (set to 1 in
 *   ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h). The initialization is triggered
 *   by lv_init() -> lv_draw_dave2d_init() -> lv_dave2d_init().
 *
 *   LVGL's internal initialization sequence (lv_dave2d_init):
 *     1. d2_opendevice(0)              - Create Dave2D device context
 *     2. d2_inithw(handle, 0)          - Initialize Dave2D hardware
 *     3. d2_setblendmode()             - Set alpha blending mode
 *     4. d2_setalphamode()             - Set constant alpha mode
 *     5. d2_setalpha(UINT8_MAX)        - Set full opacity
 *     6. d2_setantialiasing(1)         - Enable antialiasing
 *     7. d2_setlinecap(d2_lc_butt)     - Set line cap style
 *     8. d2_setlinejoin(d2_lj_miter)   - Set line join style
 *     9. d2_setdlistblocksize(25)      - Set display list block size
 *    10. d2_newrenderbuffer() x2       - Create blit and main render buffers
 *    11. d2_selectrenderbuffer()       - Select main render buffer
 *
 *   Reference: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-604
 *
 * This Port Layer's Role:
 *   Since LVGL already performs the Dave2D initialization, this port layer
 *   does NOT duplicate the init sequence. Instead, it provides:
 *     - Post-init validation: verifies Dave2D was initialized by LVGL
 *     - Status tracking: records whether Dave2D is available
 *     - Hardware info query: HW revision, driver version
 *     - Diagnostic output: NT-Shell "dave2d status" command
 *     - Clean shutdown support: dave2d_port_deinit() for orderly shutdown
 *
 *   The LVGL internal handle (_d2_handle) is accessed via the extern
 *   declaration from lv_draw_dave2d.c. The FSP-generated d2_handle0
 *   (common_data.c) is a separate variable not used by this module.
 *
 * Dave2D Capabilities (RA8P1 D/AVE 2D v3.17):
 *   - Hardware-accelerated rectangle fill (d2_renderbox)
 *   - Hardware-accelerated line drawing (d2_renderline)
 *   - Hardware-accelerated arc/circle drawing (d2_renderwedge)
 *   - Texture mapping with bilinear filtering
 *   - BLIT with scaling (d2_setblitsrc + d2_blitcopy)
 *   - Alpha blending (per-pixel and per-primitive)
 *   - Antialiasing for all primitives
 *   - Triangle rendering
 *   - Display list based command processing
 *
 * LVGL Integration:
 *   When LV_USE_DRAW_DAVE2D = 1, LVGL registers a Dave2D draw unit that
 *   evaluates each draw task. If Dave2D can accelerate the operation
 *   (rectangle fill, line, image blit, label, arc, triangle), it claims
 *   the task and uses the hardware engine. Otherwise, the software renderer
 *   handles it.
 *
 * Reference:
 *   - LVGL Dave2D init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:76-108
 *   - LVGL Dave2D hw init: e2studio_CPU0/ra/lvgl/lvgl/src/draw/renesas/dave2d/lv_draw_dave2d.c:541-604
 *   - Dave2D driver API: e2studio_CPU0/ra/tes/dave2d/inc/dave_driver.h
 *   - FSP d2_handle0: e2studio_CPU0/ra_gen/common_data.c:4
 *   - LV_USE_DRAW_DAVE2D: e2studio_CPU0/ra_cfg/fsp_cfg/lvgl/lvgl/lv_conf.h:107
 *
 * @note
 * This file is part of the Dave2D control (S-004-1) implementation.
 */

#ifndef DAVE2D_PORT_H
#define DAVE2D_PORT_H

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdint.h>
#include <stdbool.h>

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Global Typedef definitions
 *********************************************************************************************************************/

/** Dave2D initialization status */
typedef enum {
    DAVE2D_STATUS_NOT_INITIALIZED = 0,  /**< Not yet verified / LVGL not initialized */
    DAVE2D_STATUS_INITIALIZED,          /**< LVGL has initialized Dave2D successfully */
    DAVE2D_STATUS_NOT_AVAILABLE,        /**< LV_USE_DRAW_DAVE2D is disabled */
    DAVE2D_STATUS_ERROR,                /**< LVGL Dave2D initialization failed */
} dave2d_status_t;

/**
 * Dave2D hardware/driver information structure
 *
 * Contains version information and hardware capabilities for diagnostics.
 */
typedef struct {
    uint32_t    driver_version;         /**< Dave2D driver library version (d2_getversion) */
    const char *driver_version_str;     /**< Dave2D driver version string (d2_getversionstring) */
    uint32_t    hw_revision;            /**< Dave2D hardware revision (d2_getrevisionhw) */
    const char *hw_revision_str;        /**< Dave2D hardware revision string (d2_getrevisionstringhw) */
    bool        handle_valid;           /**< true if the LVGL Dave2D device handle is valid */
    bool        lvgl_dave2d_enabled;    /**< true if LV_USE_DRAW_DAVE2D is enabled */
} dave2d_info_t;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Verify and record Dave2D initialization status
 *
 * @details This function checks whether LVGL has successfully initialized
 *          the Dave2D hardware by examining the LVGL internal handle
 *          (_d2_handle). It does NOT perform the Dave2D initialization
 *          itself -- that is done by LVGL's lv_draw_dave2d_init().
 *
 *          Call this function AFTER lv_init() has completed.
 *
 * Preconditions:
 *   - lv_init() must have been called (which triggers lv_draw_dave2d_init())
 *
 * @retval true   Dave2D is available (LVGL initialized it successfully)
 * @retval false  Dave2D is not available (disabled or init failed)
 */
bool dave2d_port_init(void);

/**
 * Deinitialize the Dave2D port layer
 *
 * @details Performs orderly shutdown of the Dave2D tracking. In the current
 *          architecture, the actual Dave2D hardware deinitialization is
 *          handled by LVGL's cleanup path. This function resets the
 *          tracking state.
 *
 *          This is provided for completeness of the init/deinit lifecycle.
 */
void dave2d_port_deinit(void);

/**
 * Get the current Dave2D initialization status
 *
 * @return Current Dave2D status
 */
dave2d_status_t dave2d_port_get_status(void);

/**
 * Check if Dave2D hardware accelerator is available for use
 *
 * @retval true   Dave2D is initialized and available
 * @retval false  Dave2D is not available
 */
bool dave2d_port_is_available(void);

/**
 * Get Dave2D hardware and driver information
 *
 * @details Fills the info structure with Dave2D version information,
 *          hardware revision, and handle validity. This information is
 *          queried from the Dave2D driver library and hardware registers.
 *
 * @param info Pointer to dave2d_info_t structure to fill
 */
void dave2d_port_get_info(dave2d_info_t *info);

/**
 * NT-Shell command handler for Dave2D diagnostics
 *
 * @details Registered as the "dave2d" command in usrcmd.c.
 *          Sub-commands:
 *            dave2d status  - Show Dave2D initialization state and HW info
 *
 * @param argc Argument count
 * @param argv Argument vector
 * @return CMD_OK on success, CMD_ERR_* on error
 */
int usrcmd_dave2d(int argc, char **argv);

#ifdef __cplusplus
}
#endif

#endif /* DAVE2D_PORT_H */
