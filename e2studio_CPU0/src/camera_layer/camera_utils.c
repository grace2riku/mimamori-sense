/**
 * @file camera_utils.c
 * @brief Camera image preprocessing for AI inference (F-003-6)
 * @details
 * Implements the image conversion pipeline to transform camera-captured
 * RGB565 frames into the fall detection AI model's input format
 * (192x192x3 RGB INT8).
 *
 * The conversion is performed in a single pass with no intermediate
 * buffer allocation:
 *   1. Center-crop: extract a square region from the center of the
 *      wider input image (e.g., 450x450 from 768x450)
 *   2. Nearest-neighbor downscale to the model input size (192x192)
 *   3. RGB565 to RGB888 expansion (5/6/5 bits -> 8/8/8 bits)
 *   4. Unsigned-to-signed conversion: uint8 [0..255] -> int8 [-128..+127]
 *
 * Performance is measured using the ARM DWT cycle counter (CYCCNT),
 * which is available on Cortex-M85 without requiring any FSP timer
 * peripheral configuration.
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/camera_layer/camera_utils.c
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "hal_data.h"
#include "camera_utils.h"

/**********************************************************************************************************************
 Macro definitions
 *********************************************************************************************************************/

/**********************************************************************************************************************
 Private (static) variables
 *********************************************************************************************************************/

/** Last preprocessing execution time in milliseconds */
static volatile uint32_t s_preproc_time_ms = 0;

/** Total preprocessing call count */
static volatile uint32_t s_preproc_count = 0;

/**********************************************************************************************************************
 Private (static) function prototypes
 *********************************************************************************************************************/

static uint32_t dwt_get_cycles(void);
static uint32_t dwt_cycles_to_ms(uint32_t start, uint32_t end);

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Convert RGB565 camera image to RGB INT8 for fall detection model
 *
 * Reference: face_detection/src/camera_layer/camera_utils.c
 *            image_rgb565_to_int8() lines 35-80 (grayscale version)
 *            image_rgb565_to_rgb888() lines 82-119 (RGB888 version)
 *
 * This function combines the crop+resize logic from image_rgb565_to_int8()
 * with the RGB channel extraction from image_rgb565_to_rgb888(), and adds
 * the unsigned-to-signed INT8 conversion needed for the quantized model.
 */
vision_ai_app_err_t image_rgb565_to_rgb_int8(const void *p_input_image_buff,
                                              void *p_output_image_buff,
                                              uint16_t in_width,
                                              uint16_t in_height,
                                              uint16_t out_width,
                                              uint16_t out_height)
{
    vision_ai_app_err_t status = VISION_AI_APP_SUCCESS;

    /* Validate pointers */
    /* Reference: camera_utils.c lines 40-44 */
    if ((NULL == p_input_image_buff) || (NULL == p_output_image_buff))
    {
        return VISION_AI_APP_ERR_NULL_POINTER;
    }

    /* The crop logic assumes width >= height (landscape orientation) */
    /* Reference: camera_utils.c lines 46-50 */
    if (in_width < in_height)
    {
        return VISION_AI_APP_ERR_IMG_PROCESS;
    }

    /* Start timing using DWT cycle counter */
    uint32_t t_start = dwt_get_cycles();

    register const uint16_t *p_input  = (const uint16_t *)p_input_image_buff;
    register int8_t         *p_output = (int8_t *)p_output_image_buff;

    /*
     * Center-crop offset: skip pixels on each side to extract a square
     * region from the wider input image.
     *
     * Example: 768x450 input -> crop_offset = (768 - 450) / 2 = 159
     *          This extracts columns [159..608] (450 pixels wide)
     *
     * Reference: camera_utils.c line 56
     */
    const uint32_t crop_offset = (uint32_t)(in_width - in_height) / 2u;

    /*
     * Single-pass: iterate over every output pixel, compute the
     * corresponding source pixel via nearest-neighbor mapping from
     * the cropped square region, extract RGB channels, and convert
     * to signed INT8.
     *
     * Reference: camera_utils.c lines 58-77 (grayscale loop)
     *            camera_utils.c lines 98-118 (RGB888 loop)
     */
    for (uint32_t y = 0; y < out_height; y++)
    {
        /* Row offset into the input image for this output row */
        /* Reference: camera_utils.c line 63 */
        const uint32_t y_offset = in_width * ((uint32_t)in_height * y / out_height);

        /* Pointer to the start of the cropped region in this row */
        /* Reference: camera_utils.c line 66 */
        const uint16_t *p_input_base = p_input + crop_offset + y_offset;

        for (uint32_t x = 0; x < out_width; x++)
        {
            /* Nearest-neighbor X mapping within the cropped square */
            /* Reference: camera_utils.c line 69 */
            const uint16_t pixel = *(p_input_base + ((uint32_t)in_height * x / out_width));

            /*
             * Extract RGB channels from RGB565:
             *   R: bits [15:11] -> 5 bits
             *   G: bits [10:5]  -> 6 bits
             *   B: bits [4:0]   -> 5 bits
             *
             * Scale to full 8-bit range:
             *   R: r5 * 255 / 31 (equivalent to (r5 << 3) | (r5 >> 2))
             *   G: g6 * 255 / 63 (equivalent to (g6 << 2) | (g6 >> 4))
             *   B: b5 * 255 / 31 (equivalent to (b5 << 3) | (b5 >> 2))
             *
             * Reference: camera_utils.c lines 104-110
             */
            uint8_t r5 = (uint8_t)((pixel >> 11) & 0x1Fu);
            uint8_t g6 = (uint8_t)((pixel >> 5)  & 0x3Fu);
            uint8_t b5 = (uint8_t)(pixel & 0x1Fu);

            uint8_t r8 = (uint8_t)((r5 << 3) | (r5 >> 2));
            uint8_t g8 = (uint8_t)((g6 << 2) | (g6 >> 4));
            uint8_t b8 = (uint8_t)((b5 << 3) | (b5 >> 2));

            /*
             * Convert to signed INT8 [-128, +127]:
             *   int8_value = uint8_value - 128
             *
             * This matches the quantization scheme used by the YOLOv8 pico
             * INT8 model (zero_point = -128, scale = 1/255).
             *
             * Reference: camera_utils.c line 75 (grayscale version)
             */
            *p_output++ = (int8_t)(r8 - 128u);
            *p_output++ = (int8_t)(g8 - 128u);
            *p_output++ = (int8_t)(b8 - 128u);
        }
    }

    /* Stop timing */
    uint32_t t_end = dwt_get_cycles();
    s_preproc_time_ms = dwt_cycles_to_ms(t_start, t_end);
    s_preproc_count++;

    return status;
}

/**
 * Get the last preprocessing execution time in milliseconds
 */
uint32_t camera_utils_get_preproc_time_ms(void)
{
    return s_preproc_time_ms;
}

/**
 * Get the total number of preprocessing calls since boot
 */
uint32_t camera_utils_get_preproc_count(void)
{
    return s_preproc_count;
}

/**********************************************************************************************************************
 Private (static) functions
 *********************************************************************************************************************/

/**
 * Read the DWT cycle counter (CYCCNT)
 *
 * @details
 * Uses the ARM DWT (Data Watchpoint and Trace) cycle counter for
 * high-resolution timing. On Cortex-M85 @ 1GHz, 1 cycle = 1ns,
 * and CYCCNT wraps around every ~4.3 seconds (2^32 cycles).
 *
 * The DWT CYCCNT is enabled at startup by the trace enable bit
 * in CoreDebug->DEMCR. If not already enabled, this function
 * enables it on first call.
 *
 * Reference: face_detection/src/time_counter/time_counter.c lines 27-36
 *            (TIMER_TYPE == 0, CPU DWT mode)
 *
 * @return Current cycle count value
 */
static uint32_t dwt_get_cycles(void)
{
    /* Ensure DWT cycle counter is enabled */
    if (0u == (DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk))
    {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
        DWT->CYCCNT = 0u;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    return DWT->CYCCNT;
}

/**
 * Convert DWT cycle count difference to milliseconds
 *
 * @details
 * Uses the CPU clock frequency (CPUCLK) to convert cycle counts
 * to milliseconds. On RA8P1, CPUCLK = 1GHz, so:
 *   ms = (end - start) / (1,000,000,000 / 1000) = (end - start) / 1,000,000
 *
 * Handles CYCCNT wraparound correctly since unsigned subtraction
 * gives the correct elapsed cycles even after a 32-bit wrap.
 *
 * Reference: face_detection/src/time_counter/time_counter.c lines 92-103
 *            (TIMER_TYPE == 0 path)
 *
 * @param start  Start cycle count (from dwt_get_cycles)
 * @param end    End cycle count (from dwt_get_cycles)
 * @return Elapsed time in milliseconds
 */
static uint32_t dwt_cycles_to_ms(uint32_t start, uint32_t end)
{
    uint32_t elapsed_cycles = end - start;  /* handles wraparound */
#if BSP_FEATURE_CGC_HAS_CPUCLK
    uint32_t cpu_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_CPUCLK);
#else
    uint32_t cpu_hz = R_FSP_SystemClockHzGet(FSP_PRIV_CLOCK_ICLK);
#endif
    /* Avoid division by zero; cpu_hz should never be 0 on a running system */
    if (cpu_hz == 0u)
    {
        return 0u;
    }
    return (uint32_t)((uint64_t)elapsed_cycles * 1000u / cpu_hz);
}
