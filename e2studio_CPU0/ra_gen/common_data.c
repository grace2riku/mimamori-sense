/* generated common source file - do not edit */
#include "common_data.h"
icu_instance_ctrl_t g_external_irq0_ctrl;

/** External IRQ extended configuration for ICU HAL driver */
const icu_extended_cfg_t g_external_irq0_ext_cfg = { .filter_src =
		EXTERNAL_IRQ_DIGITAL_FILTER_PCLK_DIV, };

const external_irq_cfg_t g_external_irq0_cfg = { .channel = 19, .trigger =
		EXTERNAL_IRQ_TRIG_FALLING, .filter_enable = true, .clock_source_div =
		EXTERNAL_IRQ_CLOCK_SOURCE_DIV_1, .p_callback = touch_irq_callback,
/** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
		.p_context = (void*) &NULL,
#endif
		.p_extend = (void*) &g_external_irq0_ext_cfg, .ipl = (12),
#if defined(VECTOR_NUMBER_ICU_IRQ19)
    .irq                 = VECTOR_NUMBER_ICU_IRQ19,
#else
		.irq = FSP_INVALID_VECTOR,
#endif
		};
/* Instance structure to use this module. */
const external_irq_instance_t g_external_irq0 = { .p_ctrl =
		&g_external_irq0_ctrl, .p_cfg = &g_external_irq0_cfg, .p_api =
		&g_external_irq_on_icu };
iic_master_instance_ctrl_t g_i2c_master0_ctrl;
const iic_master_extended_cfg_t g_i2c_master0_extend =
		{ .timeout_mode = IIC_MASTER_TIMEOUT_MODE_SHORT, .timeout_scl_low =
				IIC_MASTER_TIMEOUT_SCL_LOW_ENABLED, .smbus_operation = 0,
				/* Actual calculated bitrate: 393082. Actual calculated duty cycle: 50%. */.clock_settings.brl_value =
						15, .clock_settings.brh_value = 15,
				.clock_settings.cks_value = 2, .clock_settings.sddl_value = 0,
				.clock_settings.dlcs_value = 0, };
const i2c_master_cfg_t g_i2c_master0_cfg = { .channel = 1, .rate =
		I2C_MASTER_RATE_FAST, .slave = 0x38, .addr_mode =
		I2C_MASTER_ADDR_MODE_7BIT,
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_tx = NULL,
#else
                .p_transfer_tx       = &RA_NOT_DEFINED,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_rx = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
		.p_callback = rm_comms_i2c_callback, .p_context = NULL,
#if defined(VECTOR_NUMBER_IIC1_RXI)
    .rxi_irq             = VECTOR_NUMBER_IIC1_RXI,
#else
		.rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TXI)
    .txi_irq             = VECTOR_NUMBER_IIC1_TXI,
#else
		.txi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_TEI)
    .tei_irq             = VECTOR_NUMBER_IIC1_TEI,
#else
		.tei_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_IIC1_ERI)
    .eri_irq             = VECTOR_NUMBER_IIC1_ERI,
#else
		.eri_irq = FSP_INVALID_VECTOR,
#endif
		.ipl = (12), .p_extend = &g_i2c_master0_extend, };
/* Instance structure to use this module. */
const i2c_master_instance_t g_i2c_master0 = { .p_ctrl = &g_i2c_master0_ctrl,
		.p_cfg = &g_i2c_master0_cfg, .p_api = &g_i2c_master_on_iic };
#if BSP_CFG_RTOS
#if BSP_CFG_RTOS == 1
#if !defined(g_comms_i2c_bus0_recursive_mutex)
TX_MUTEX g_comms_i2c_bus0_recursive_mutex_handle;
CHAR g_comms_i2c_bus0_recursive_mutex_name[] = "g_comms_i2c_bus0 recursive mutex";
#endif
#if !defined(g_comms_i2c_bus0_blocking_semaphore)
TX_SEMAPHORE g_comms_i2c_bus0_blocking_semaphore_handle;
CHAR g_comms_i2c_bus0_blocking_semaphore_name[] = "g_comms_i2c_bus0 blocking semaphore";
#endif
#elif BSP_CFG_RTOS == 2
#if !defined(g_comms_i2c_bus0_recursive_mutex)
SemaphoreHandle_t g_comms_i2c_bus0_recursive_mutex_handle;
StaticSemaphore_t g_comms_i2c_bus0_recursive_mutex_memory;
#endif
#if !defined(g_comms_i2c_bus0_blocking_semaphore)
SemaphoreHandle_t g_comms_i2c_bus0_blocking_semaphore_handle;
StaticSemaphore_t g_comms_i2c_bus0_blocking_semaphore_memory;
#endif
#endif

#if !defined(g_comms_i2c_bus0_recursive_mutex)
/* Recursive Mutex for I2C bus */
rm_comms_i2c_mutex_t g_comms_i2c_bus0_recursive_mutex =
{
    .p_mutex_handle = &g_comms_i2c_bus0_recursive_mutex_handle,
#if BSP_CFG_RTOS == 1 // ThradX
    .p_mutex_name = &g_comms_i2c_bus0_recursive_mutex_name[0],
#elif BSP_CFG_RTOS == 2 // FreeRTOS
    .p_mutex_memory = &g_comms_i2c_bus0_recursive_mutex_memory,
#endif
};
#endif

#if !defined(g_comms_i2c_bus0_blocking_semaphore)
/* Semaphore for blocking */
rm_comms_i2c_semaphore_t g_comms_i2c_bus0_blocking_semaphore =
{
    .p_semaphore_handle = &g_comms_i2c_bus0_blocking_semaphore_handle,
#if BSP_CFG_RTOS == 1 // ThreadX
    .p_semaphore_name = &g_comms_i2c_bus0_blocking_semaphore_name[0],
#elif BSP_CFG_RTOS == 2 // FreeRTOS
    .p_semaphore_memory = &g_comms_i2c_bus0_blocking_semaphore_memory,
#endif
};
#endif
#endif

/* Shared I2C Bus */
#define RA_NOT_DEFINED (1)
rm_comms_i2c_bus_extended_cfg_t g_comms_i2c_bus0_extended_cfg = {
#if !defined(g_i2c_master0)
		.p_driver_instance = (void*) &g_i2c_master0,
#elif !defined(RA_NOT_DEFINED)
    .p_driver_instance      = (void*)&RA_NOT_DEFINED,
#elif !defined(RA_NOT_DEFINED)
    .p_driver_instance      = (void*)&RA_NOT_DEFINED,
#endif
		.p_current_ctrl = NULL, .bus_timeout = 0xFFFFFFFF,
#if BSP_CFG_RTOS
#if !defined(g_comms_i2c_bus0_blocking_semaphore)
    .p_blocking_semaphore = &g_comms_i2c_bus0_blocking_semaphore,
#if !defined(g_comms_i2c_bus0_recursive_mutex)
    .p_bus_recursive_mutex = &g_comms_i2c_bus0_recursive_mutex,
#else
    .p_bus_recursive_mutex = NULL,
#endif
#else
    .p_bus_recursive_mutex = NULL,
    .p_blocking_semaphore = NULL,
#endif
#else
#endif

#if (0)
    .p_elc = (void*)&g_elc,
    .p_timer = (void*)&g_timer,
#else
		.p_elc = NULL, .p_timer = NULL,
#endif
		};
#undef RA_NOT_DEFINED
const uint8_t DRW_INT_IPL = (2);
d2_device *d2_handle0;
/** Display framebuffer */
#if GLCDC_CFG_LAYER_1_ENABLE
        uint8_t fb_background[2][DISPLAY_BUFFER_STRIDE_BYTES_INPUT0 * DISPLAY_VSIZE_INPUT0] BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit_nocache");
        #else
/** Graphics Layer 1 is specified not to be used when starting */
#endif
#if GLCDC_CFG_LAYER_2_ENABLE
        uint8_t fb_foreground[2][DISPLAY_BUFFER_STRIDE_BYTES_INPUT1 * DISPLAY_VSIZE_INPUT1] BSP_ALIGN_VARIABLE(64) BSP_PLACE_IN_SECTION(BSP_UNINIT_SECTION_PREFIX ".sdram_noinit");
        #else
/** Graphics Layer 2 is specified not to be used when starting */
#endif

#if GLCDC_CFG_CLUT_ENABLE
        /** Display CLUT buffer to be used for updating CLUT */
        static uint32_t CLUT_buffer[256];

        /** Display CLUT configuration(only used if using CLUT format) */
        display_clut_cfg_t g_display0_clut_cfg_glcdc =
        {
            .p_base              = (uint32_t *)CLUT_buffer,
            .start               = 0,   /* User have to update this setting when using */
            .size                = 256  /* User have to update this setting when using */
        };
        #else
/** CLUT is specified not to be used */
#endif

#if (false)
         #define GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST   const
         #define GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST    (uint16_t *)
         #define GLCDC_CFG_CORRECTION_GAMMA_CFG_CAST      (display_gamma_correction_t *)
        #else
#define GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST
#define GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST
#define GLCDC_CFG_CORRECTION_GAMMA_CFG_CAST
#endif

#if ((GLCDC_CFG_CORRECTION_GAMMA_ENABLE_R | GLCDC_CFG_CORRECTION_GAMMA_ENABLE_G | GLCDC_CFG_CORRECTION_GAMMA_ENABLE_B) && GLCDC_CFG_COLOR_CORRECTION_ENABLE)
        /** Gamma correction tables */
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_R
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_r_gain[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024};
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_r_threshold[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
        #endif
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_G
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_g_gain[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024};
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_g_threshold[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
        #endif
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_B
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_b_gain[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024, 1024};
        static GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST uint16_t glcdc_gamma_b_threshold[DISPLAY_GAMMA_CURVE_ELEMENT_NUM] = {0, 64, 128, 192, 256, 320, 384, 448, 512, 576, 640, 704, 768, 832, 896, 960};
        #endif
        GLCDC_CFG_CORRECTION_GAMMA_TABLE_CONST display_gamma_correction_t g_display0_gamma_cfg =
        {
            .r =
            {
                .enable      = GLCDC_CFG_CORRECTION_GAMMA_ENABLE_R,
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_R
                .gain        = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_r_gain,
                .threshold   = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_r_threshold
        #else
                .gain        = NULL,
                .threshold   = NULL
        #endif
            },
            .g =
            {
                .enable      = GLCDC_CFG_CORRECTION_GAMMA_ENABLE_G,
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_G
                .gain        = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_g_gain,
                .threshold   = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_g_threshold
        #else
                .gain        = NULL,
                .threshold   = NULL
        #endif
            },
            .b =
            {
                .enable      = GLCDC_CFG_CORRECTION_GAMMA_ENABLE_B,
        #if GLCDC_CFG_CORRECTION_GAMMA_ENABLE_B
                .gain        = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_b_gain,
                .threshold   = GLCDC_CFG_CORRECTION_GAMMA_TABLE_CAST glcdc_gamma_b_threshold
        #else
                .gain        = NULL,
                .threshold   = NULL
        #endif
            }
        };
        #endif

#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED != RA_NOT_DEFINED)
          const mipi_dsi_instance_t RA_NOT_DEFINED;
        #endif
/** Display device extended configuration */
const glcdc_extended_cfg_t g_display0_extend_cfg = { .tcon_hsync =
		GLCDC_TCON_PIN_NONE, .tcon_vsync = GLCDC_TCON_PIN_NONE, .tcon_de =
		GLCDC_TCON_PIN_2, .correction_proc_order =
		GLCDC_CORRECTION_PROC_ORDER_BRIGHTNESS_CONTRAST2GAMMA, .clksrc =
		GLCDC_CLK_SRC_INTERNAL, .clock_div_ratio = GLCDC_PANEL_CLK_DIVISOR_4,
		.dithering_mode = GLCDC_DITHERING_MODE_TRUNCATE, .dithering_pattern_A =
				GLCDC_DITHERING_PATTERN_11, .dithering_pattern_B =
				GLCDC_DITHERING_PATTERN_11, .dithering_pattern_C =
				GLCDC_DITHERING_PATTERN_11, .dithering_pattern_D =
				GLCDC_DITHERING_PATTERN_11,
#if (RA_NOT_DEFINED != RA_NOT_DEFINED)
            .phy_layer             = (void*)&RA_NOT_DEFINED
        #else
		.phy_layer = NULL
#endif
		};
#undef RA_NOT_DEFINED

/** Display control block instance */
glcdc_instance_ctrl_t g_display0_ctrl;

/** Display interface configuration */
const display_cfg_t g_display0_cfg =
		{
		/** Input1(Graphics1 layer) configuration */
		.input[0] =
		{
#if GLCDC_CFG_LAYER_1_ENABLE
                .p_base              = (uint32_t *)&fb_background[0],
                #else
				.p_base = NULL,
#endif
				.hsize = DISPLAY_HSIZE_INPUT0, .vsize = DISPLAY_VSIZE_INPUT0,
				.hstride = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0, .format =
						DISPLAY_IN_FORMAT_16BITS_RGB565,
				.line_descending_enable = false, .lines_repeat_enable = false,
				.lines_repeat_times = 0 },

		/** Input2(Graphics2 layer) configuration */
		.input[1] =
		{
#if GLCDC_CFG_LAYER_2_ENABLE
                .p_base              = (uint32_t *)&fb_foreground[0],
                #else
				.p_base = NULL,
#endif
				.hsize = DISPLAY_HSIZE_INPUT1, .vsize = DISPLAY_VSIZE_INPUT1,
				.hstride = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT1, .format =
						DISPLAY_IN_FORMAT_16BITS_RGB565,
				.line_descending_enable = false, .lines_repeat_enable = false,
				.lines_repeat_times = 0 },

		/** Input1(Graphics1 layer) layer configuration */
		.layer[0] =
		{ .coordinate = { .x = 0, .y = 0 }, .fade_control =
				DISPLAY_FADE_CONTROL_NONE, .fade_speed = 0 },

		/** Input2(Graphics2 layer) layer configuration */
		.layer[1] =
		{ .coordinate = { .x = 0, .y = 0 }, .fade_control =
				DISPLAY_FADE_CONTROL_NONE, .fade_speed = 0 },

				/** Output configuration */
				.output =
						{ .htiming = { .total_cyc = 1344, .display_cyc = 1024,
								.back_porch = 160, .sync_width = 4,
								.sync_polarity =
										DISPLAY_SIGNAL_POLARITY_LOACTIVE },
								.vtiming =
										{ .total_cyc = 635, .display_cyc = 600,
												.back_porch = 23, .sync_width =
														3,
												.sync_polarity =
														DISPLAY_SIGNAL_POLARITY_LOACTIVE },
								.format = DISPLAY_OUT_FORMAT_24BITS_RGB888,
								.endian = DISPLAY_ENDIAN_LITTLE, .color_order =
										DISPLAY_COLOR_ORDER_RGB,
								.data_enable_polarity =
										DISPLAY_SIGNAL_POLARITY_HIACTIVE,
								.sync_edge = DISPLAY_SIGNAL_SYNC_EDGE_FALLING,
								.bg_color = { .byte = { .a = 255, .r = 0,
										.g = 0, .b = 0 } },
#if (GLCDC_CFG_COLOR_CORRECTION_ENABLE)
                .brightness =
                {
                    .enable          = false,
                    .r               = 512,
                    .g               = 512,
                    .b               = 512
                },
                .contrast =
                {
                    .enable          = false,
                    .r               = 128,
                    .g               = 128,
                    .b               = 128
                },
#if (GLCDC_CFG_CORRECTION_GAMMA_ENABLE_R | GLCDC_CFG_CORRECTION_GAMMA_ENABLE_G | GLCDC_CFG_CORRECTION_GAMMA_ENABLE_B)
 #if false
                .p_gamma_correction  = GLCDC_CFG_CORRECTION_GAMMA_CFG_CAST (&g_display0_gamma_cfg),
 #else
                .p_gamma_correction  = &g_display0_gamma_cfg,
 #endif
#else
                .p_gamma_correction  = NULL,
#endif
#endif
								.dithering_on = false },

				/** Display device callback function pointer */
				.p_callback = _rm_lvgl_port_display_callback, .p_context = NULL,

				/** Display device extended configuration */
				.p_extend = (void*) (&g_display0_extend_cfg),

				.line_detect_ipl = (12), .underflow_1_ipl = (12),
				.underflow_2_ipl = (BSP_IRQ_DISABLED),

#if defined(VECTOR_NUMBER_GLCDC_LINE_DETECT)
            .line_detect_irq        = VECTOR_NUMBER_GLCDC_LINE_DETECT,
#else
				.line_detect_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GLCDC_UNDERFLOW_1)
            .underflow_1_irq        = VECTOR_NUMBER_GLCDC_UNDERFLOW_1,
#else
				.underflow_1_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GLCDC_UNDERFLOW_2)
            .underflow_2_irq        = VECTOR_NUMBER_GLCDC_UNDERFLOW_2,
#else
				.underflow_2_irq = FSP_INVALID_VECTOR,
#endif
		};

#if GLCDC_CFG_LAYER_1_ENABLE
        /** Display on GLCDC run-time configuration(for the graphics1 layer) */
        display_runtime_cfg_t g_display0_runtime_cfg_bg =
        {
            .input =
            {
                #if (true)
                .p_base              = (uint32_t *)&fb_background[0],
                #else
                .p_base              = NULL,
                #endif
                .hsize               = DISPLAY_HSIZE_INPUT0,
                .vsize               = DISPLAY_VSIZE_INPUT0,
                .hstride             = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT0,
                .format              = DISPLAY_IN_FORMAT_16BITS_RGB565,
                .line_descending_enable = false,
                .lines_repeat_enable = false,
                .lines_repeat_times  = 0
            },
            .layer =
            {
                .coordinate = {
                        .x           = 0,
                        .y           = 0
                },
                .fade_control        = DISPLAY_FADE_CONTROL_NONE,
                .fade_speed          = 0
            }
        };
#endif
#if GLCDC_CFG_LAYER_2_ENABLE
        /** Display on GLCDC run-time configuration(for the graphics2 layer) */
        display_runtime_cfg_t g_display0_runtime_cfg_fg =
        {
            .input =
            {
                #if (false)
                .p_base              = (uint32_t *)&fb_foreground[0],
                #else
                .p_base              = NULL,
                #endif
                .hsize               = DISPLAY_HSIZE_INPUT1,
                .vsize               = DISPLAY_VSIZE_INPUT1,
                .hstride             = DISPLAY_BUFFER_STRIDE_PIXELS_INPUT1,
                .format              = DISPLAY_IN_FORMAT_16BITS_RGB565,
                .line_descending_enable = false,
                .lines_repeat_enable = false,
                .lines_repeat_times  = 0
             },
            .layer =
            {
                .coordinate = {
                        .x           = 0,
                        .y           = 0
                },
                .fade_control        = DISPLAY_FADE_CONTROL_NONE,
                .fade_speed          = 0
            }
        };
#endif

/* Instance structure to use this module. */
const display_instance_t g_display0 = { .p_ctrl = &g_display0_ctrl, .p_cfg =
		(display_cfg_t*) &g_display0_cfg, .p_api =
		(display_api_t*) &g_display_on_glcdc };
/* Display Driver Frame Buffer 0 Configuration */
#if (1 == 1) /* Inherit Frame Buffer Name from Graphics Screen 1 */
#define LVGL_FRAMEBUFFER_0 (&fb_background[0]) /* Always array[0] is used */
#else /* Inherit Frame Buffer Name from Graphics Screen 2 */
             #define LVGL_FRAMEBUFFER_0 (&fb_foreground[0]) /* Always array[0] is used */
            #endif

/* Display Driver Frame Buffer 1 Configuration */
#if (1 == 1) /* Inherit Frame Buffer Name from Graphics Screen 1 */
#if (2 > 1) /* Multiple frame buffers are used for Graphics Screen 1 */
#define LVGL_FRAMEBUFFER_1 (&fb_background[1]) /* Always array[1] is used */
#else /* Single Frame Buffer is used for Graphics Screen 1 */
              #define LVGL_FRAMEBUFFER_1 (NULL)
             #endif
#else /* Inherit Frame Buffer Name from Graphics Screen 2 */
             #if (2 > 1) /* Multiple frame buffers are used for Graphics Screen 2 */
              #define LVGL_FRAMEBUFFER_1 (&fb_foreground[1]) /* Always array[1] is used */
             #else /* Single Frame Buffer is used for Graphics Screen 2 */
              #define LVGL_FRAMEBUFFER_1 (NULL)
             #endif
            #endif

/** LVGL Port module control block instance */
rm_lvgl_port_instance_ctrl_t g_lvgl_port_ctrl;

/** LVGL Port module configuration */
const rm_lvgl_port_cfg_t g_lvgl_port_cfg = { .p_display_instance =
		(display_instance_t*) &g_display0,
#if (1 == 1)
		.inherit_frame_layer = DISPLAY_FRAME_LAYER_1,
#else
	            .inherit_frame_layer = DISPLAY_FRAME_LAYER_2,
            #endif
		.p_framebuffer_0 = LVGL_FRAMEBUFFER_0, .p_framebuffer_1 =
				LVGL_FRAMEBUFFER_1, .p_callback = lvgl_glcdc_callback, };
ioport_instance_ctrl_t g_ioport_ctrl;
const ioport_instance_t g_ioport = { .p_api = &g_ioport_on_ioport, .p_ctrl =
		&g_ioport_ctrl, .p_cfg = &g_bsp_pin_cfg, };
EventGroupHandle_t g_i2c_event_group;
#if 1
StaticEventGroup_t g_i2c_event_group_memory;
#endif
void rtos_startup_err_callback(void *p_instance, void *p_data);
SemaphoreHandle_t g_irq_binary_semaphore;
#if 1
StaticSemaphore_t g_irq_binary_semaphore_memory;
#endif
void rtos_startup_err_callback(void *p_instance, void *p_data);
void g_common_init(void) {
	g_i2c_event_group =
#if 1
			xEventGroupCreateStatic(&g_i2c_event_group_memory);
#else
                xEventGroupCreate();
                #endif
	if (NULL == g_i2c_event_group) {
		rtos_startup_err_callback(g_i2c_event_group, 0);
	}
	g_irq_binary_semaphore =
#if 1
			xSemaphoreCreateBinaryStatic(&g_irq_binary_semaphore_memory);
#else
                xSemaphoreCreateBinary();
                #endif
	if (NULL == g_irq_binary_semaphore) {
		rtos_startup_err_callback(g_irq_binary_semaphore, 0);
	}
}
