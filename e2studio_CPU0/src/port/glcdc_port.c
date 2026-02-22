/**
 * @file glcdc_port.c
 * @brief GLCDC (Graphics LCD Controller) port layer implementation
 * @details
 * Implements GLCDC configuration query and diagnostic functions for the
 * 1024x600 LCD panel on the EK-RA8P1 Parallel Graphics Expansion Board.
 *
 * This module provides:
 *   - Timing parameter query (resolution, porches, sync widths)
 *   - Clock configuration query (LCDCLK, pixel clock, frame rate)
 *   - Frame buffer information query (addresses, sizes, format)
 *   - NT-Shell "display" command for diagnostics
 *
 * GLCDC initialization is handled by RM_LVGL_PORT_Open() in lvgl_thread_entry.c.
 * This module only provides read-only diagnostic and query functions.
 *
 * Reference:
 *   - GLCDC configuration: e2studio_CPU0/ra_gen/common_data.c:121-232 (g_display0_cfg)
 *   - GLCDC extended config: e2studio_CPU0/ra_gen/common_data.c:99-114 (g_display0_extend_cfg)
 *   - Clock config: e2studio_CPU0/ra_gen/bsp_clock_cfg.h:57-58 (BSP_CFG_LCDCLK_*)
 *   - Display macros: e2studio_CPU0/ra_gen/common_data.h:61-64 (DISPLAY_*_INPUT0)
 *   - Timing design: doc/design/glcdc-timing-parameters.md
 *
 * @note
 * This file is part of the GLCDC control (S-002-1) implementation.
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include <stdio.h>
#include <string.h>

#include "glcdc_port.h"
#include "common_data.h"
#include "jlink_console.h"
#include "cmd_utils.h"
#include "ntlibc.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/** Console output buffer size */
#define GLCDC_PRINT_BUF_SIZE    (128)

/**********************************************************************************************************************
 Private (static) functions prototypes
 *********************************************************************************************************************/
static void glcdc_cmd_status(void);
static void glcdc_cmd_fb(void);

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * "display status" sub-command handler
 *
 * @details Displays GLCDC timing parameters, clock configuration, and
 *          output format settings.
 *
 * Reference: doc/design/glcdc-timing-parameters.md
 */
static void glcdc_cmd_status(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    glcdc_timing_info_t timing;

    glcdc_port_get_timing_info(&timing);

    /* Display resolution */
    print_to_console("[GLCDC Display Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Resolution  : %u x %u\r\n",
             timing.h_display, timing.v_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Pixel Clock : %lu MHz (%lu Hz)\r\n",
             (unsigned long)(timing.pixel_clock_hz / 1000000UL),
             (unsigned long)timing.pixel_clock_hz);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Frame Rate  : %lu.%02lu Hz\r\n",
             (unsigned long)(timing.frame_rate_x100 / 100),
             (unsigned long)(timing.frame_rate_x100 % 100));
    print_to_console(buf);

    /* Clock chain */
    print_to_console("[Clock Chain]\r\n");

    snprintf(buf, sizeof(buf), "  LCDCLK      : %lu MHz (PLL1R %lu MHz / %u)\r\n",
             (unsigned long)(GLCDC_LCDCLK_HZ / 1000000UL),
             (unsigned long)(BSP_CFG_PLL1R_FREQUENCY_HZ / 1000000UL),
             (unsigned int)(BSP_CFG_PLL1R_FREQUENCY_HZ / GLCDC_LCDCLK_HZ));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Panel Clock : %lu MHz (LCDCLK / %u)\r\n",
             (unsigned long)(timing.pixel_clock_hz / 1000000UL),
             (unsigned int)GLCDC_PANEL_CLK_DIV);
    print_to_console(buf);

    /* Horizontal timing */
    print_to_console("[Horizontal Timing]\r\n");

    snprintf(buf, sizeof(buf), "  Total       : %u cycles\r\n", timing.h_total);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Display     : %u cycles\r\n", timing.h_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Back Porch  : %u cycles\r\n", timing.h_back_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Front Porch : %u cycles\r\n", timing.h_front_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sync Width  : %u cycles\r\n", timing.h_sync_width);
    print_to_console(buf);

    /* Vertical timing */
    print_to_console("[Vertical Timing]\r\n");

    snprintf(buf, sizeof(buf), "  Total       : %u lines\r\n", timing.v_total);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Display     : %u lines\r\n", timing.v_display);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Back Porch  : %u lines\r\n", timing.v_back_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Front Porch : %u lines\r\n", timing.v_front_porch);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Sync Width  : %u lines\r\n", timing.v_sync_width);
    print_to_console(buf);

    /* Output format */
    print_to_console("[Output Format]\r\n");
    print_to_console("  Input       : RGB565 (16-bit)\r\n");
    print_to_console("  Output      : RGB888 (24-bit)\r\n");
    print_to_console("  Endian      : Little Endian\r\n");
    print_to_console("  Color Order : RGB\r\n");
    print_to_console("  DE Polarity : High Active\r\n");
    print_to_console("  Sync Edge   : Falling\r\n");

    /* TCON */
    print_to_console("[TCON]\r\n");
    print_to_console("  HSYNC       : None (internal only)\r\n");
    print_to_console("  VSYNC       : None (internal only)\r\n");
    print_to_console("  Data Enable : TCON2\r\n");
}

/**
 * "display fb" sub-command handler
 *
 * @details Displays frame buffer addresses, sizes, and format information.
 *
 * Reference: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background)
 * Reference: e2studio_CPU0/ra_gen/common_data.h:63-64 (DISPLAY_BUFFER_STRIDE_*)
 */
static void glcdc_cmd_fb(void)
{
    char buf[GLCDC_PRINT_BUF_SIZE];
    glcdc_fb_info_t fb;

    glcdc_port_get_fb_info(&fb);

    print_to_console("[Frame Buffer Configuration]\r\n");

    snprintf(buf, sizeof(buf), "  Format      : RGB565 (%u bpp)\r\n", fb.bpp);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Count: %u (double buffering)\r\n", fb.fb_count);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Stride      : %lu bytes (%lu pixels)\r\n",
             (unsigned long)fb.stride_bytes, (unsigned long)fb.stride_pixels);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Buffer Size : %lu bytes (%lu KB) per frame\r\n",
             (unsigned long)fb.fb_size_bytes,
             (unsigned long)(fb.fb_size_bytes / 1024UL));
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  Total Size  : %lu bytes (%lu KB) for %u frames\r\n",
             (unsigned long)(fb.fb_size_bytes * fb.fb_count),
             (unsigned long)(fb.fb_size_bytes * fb.fb_count / 1024UL),
             fb.fb_count);
    print_to_console(buf);

    print_to_console("[Frame Buffer Addresses]\r\n");

    snprintf(buf, sizeof(buf), "  FB[0]       : 0x%08lX\r\n",
             (unsigned long)fb.fb0_addr);
    print_to_console(buf);

    snprintf(buf, sizeof(buf), "  FB[1]       : 0x%08lX\r\n",
             (unsigned long)fb.fb1_addr);
    print_to_console(buf);

    print_to_console("[Placement]\r\n");
    print_to_console("  Section     : .sdram_noinit_nocache\r\n");
    print_to_console("  Cache       : Non-cacheable (MPU configured by BSP)\r\n");
    print_to_console("  Alignment   : 64-byte aligned\r\n");
}

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Get GLCDC timing information
 *
 * @details Fills the timing info structure with compile-time constant values
 *          derived from the FSP configuration. Also calculates the pixel clock
 *          frequency and frame rate.
 *
 * Reference:
 *   - Timing values: e2studio_CPU0/ra_gen/common_data.c:163-170
 *   - Clock values:  e2studio_CPU0/ra_gen/bsp_clock_cfg.h:57-58
 */
void glcdc_port_get_timing_info(glcdc_timing_info_t *info)
{
    if (info == NULL) {
        return;
    }

    /* Horizontal timing */
    info->h_total       = GLCDC_HTIMING_TOTAL;
    info->h_display     = GLCDC_HTIMING_DISPLAY;
    info->h_back_porch  = GLCDC_HTIMING_BACK_PORCH;
    info->h_front_porch = GLCDC_HTIMING_FRONT_PORCH;
    info->h_sync_width  = GLCDC_HTIMING_SYNC_WIDTH;

    /* Vertical timing */
    info->v_total       = GLCDC_VTIMING_TOTAL;
    info->v_display     = GLCDC_VTIMING_DISPLAY;
    info->v_back_porch  = GLCDC_VTIMING_BACK_PORCH;
    info->v_front_porch = GLCDC_VTIMING_FRONT_PORCH;
    info->v_sync_width  = GLCDC_VTIMING_SYNC_WIDTH;

    /* Pixel clock frequency */
    info->pixel_clock_hz = GLCDC_PIXEL_CLOCK_HZ;

    /*
     * Frame rate calculation:
     *   frame_rate = pixel_clock / (h_total * v_total)
     *   = 50,000,000 / (1344 * 635) = 50,000,000 / 853,440 = 58.59 Hz
     *
     * We calculate frame_rate_x100 = pixel_clock * 100 / (h_total * v_total)
     * to retain two decimal places without floating point.
     *
     * To avoid 32-bit overflow for 50MHz * 100 = 5,000,000,000 (exceeds 32-bit),
     * we divide the numerator and denominator by a common factor.
     *   frame_rate_x100 = (pixel_clock / h_total) * 100 / v_total
     *                   = (50,000,000 / 1344) * 100 / 635
     *                   = 37,202 * 100 / 635
     *                   = 5,858 (approximately 58.58 Hz)
     */
    {
        uint32_t h_total_cyc = (uint32_t)GLCDC_HTIMING_TOTAL;
        uint32_t v_total_cyc = (uint32_t)GLCDC_VTIMING_TOTAL;
        uint32_t pclk_div_h  = GLCDC_PIXEL_CLOCK_HZ / h_total_cyc;

        info->frame_rate_x100 = (pclk_div_h * 100UL) / v_total_cyc;
    }
}

/**
 * Get GLCDC frame buffer information
 *
 * @details Fills the frame buffer info structure with addresses and sizes
 *          from the FSP-generated fb_background[] array.
 *
 * Reference:
 *   - Frame buffer: e2studio_CPU0/ra_gen/common_data.c:7 (fb_background)
 *   - Stride: e2studio_CPU0/ra_gen/common_data.h:63-64
 */
void glcdc_port_get_fb_info(glcdc_fb_info_t *info)
{
    if (info == NULL) {
        return;
    }

#if GLCDC_CFG_LAYER_1_ENABLE
    info->fb0_addr      = (uint32_t)&fb_background[0];
    info->fb1_addr      = (uint32_t)&fb_background[1];
    info->stride_bytes   = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0;
    info->stride_pixels  = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0;
    info->fb_size_bytes  = DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0;
#else
    info->fb0_addr      = 0;
    info->fb1_addr      = 0;
    info->stride_bytes   = 0;
    info->stride_pixels  = 0;
    info->fb_size_bytes  = 0;
#endif

    info->bpp           = GLCDC_FB_BPP;
    info->fb_count      = GLCDC_FB_COUNT;
}

/**
 * NT-Shell "display" command handler
 *
 * @details Provides GLCDC display diagnostic sub-commands:
 *   display status - Show GLCDC timing parameters, clock, and output config
 *   display fb     - Show frame buffer addresses, sizes, and format
 *
 * Reference: doc/design/glcdc-timing-parameters.md
 */
int usrcmd_display(int argc, char **argv)
{
    if (argc < 2) {
        cmd_print_usage("display", "<subcommand>");
        print_to_console("  status  - Show GLCDC timing parameters and configuration\r\n");
        print_to_console("  fb      - Show frame buffer addresses and sizes\r\n");
        return CMD_ERR_USAGE;
    }

    if (ntlibc_strcmp(argv[1], "status") == 0) {
        glcdc_cmd_status();
        return CMD_OK;
    }

    if (ntlibc_strcmp(argv[1], "fb") == 0) {
        glcdc_cmd_fb();
        return CMD_OK;
    }

    /* Unknown sub-command */
    {
        char buf[GLCDC_PRINT_BUF_SIZE];
        snprintf(buf, sizeof(buf), "Error: Unknown sub-command '%s'.\r\n", argv[1]);
        print_to_console(buf);
    }

    cmd_print_usage("display", "<subcommand>");
    print_to_console("  status  - Show GLCDC timing parameters and configuration\r\n");
    print_to_console("  fb      - Show frame buffer addresses and sizes\r\n");

    return CMD_ERR_INVALID_ARG;
}
