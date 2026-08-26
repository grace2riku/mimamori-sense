/* generated HAL source file - do not edit */
#include "hal_data.h"

rtc_instance_ctrl_t g_rtc_ctrl;
const rtc_error_adjustment_cfg_t g_rtc_err_cfg = { .adjustment_mode =
		RTC_ERROR_ADJUSTMENT_MODE_MANUAL, .adjustment_period =
		RTC_ERROR_ADJUSTMENT_PERIOD_NONE, .adjustment_type =
		RTC_ERROR_ADJUSTMENT_NONE, .adjustment_value = 0, };
const rtc_cfg_t g_rtc_cfg = { .clock_source = RTC_CLOCK_SOURCE_SUBCLK,
		.freq_compare_value = 255, .p_err_cfg = &g_rtc_err_cfg, .p_callback =
				NULL, .p_context = NULL, .p_extend = NULL, .alarm_ipl =
				(BSP_IRQ_DISABLED), .periodic_ipl = (BSP_IRQ_DISABLED),
		.carry_ipl = (12),
#if defined(VECTOR_NUMBER_RTC_ALARM)
    .alarm_irq               = VECTOR_NUMBER_RTC_ALARM,
#else
		.alarm_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_RTC_PERIOD)
    .periodic_irq            = VECTOR_NUMBER_RTC_PERIOD,
#else
		.periodic_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_RTC_CARRY)
    .carry_irq               = VECTOR_NUMBER_RTC_CARRY,
#else
		.carry_irq = FSP_INVALID_VECTOR,
#endif
		};
/* Instance structure to use this module. */
const rtc_instance_t g_rtc = { .p_ctrl = &g_rtc_ctrl, .p_cfg = &g_rtc_cfg,
		.p_api = &g_rtc_on_rtc };
/* I2C Communication Device */
rm_comms_i2c_instance_ctrl_t g_comms_i2c_codec_ctrl;

/* Lower level driver configuration */
const i2c_master_cfg_t g_comms_i2c_codec_lower_level_cfg = { .slave = 0x1A,
		.addr_mode = I2C_MASTER_ADDR_MODE_7BIT, .p_callback =
				rm_comms_i2c_callback, };

const rm_comms_cfg_t g_comms_i2c_codec_cfg = { .semaphore_timeout = 0xFFFFFFFF,
		.p_lower_level_cfg = (void*) &g_comms_i2c_codec_lower_level_cfg,
		.p_extend = (void*) &g_comms_i2c_bus0_extended_cfg, .p_callback =
				audio_codec_i2c_callback,
#if defined(NULL)
    .p_context          = NULL,
#else
		.p_context = (void*) &NULL,
#endif
		};

const rm_comms_instance_t g_comms_i2c_codec = { .p_ctrl =
		&g_comms_i2c_codec_ctrl, .p_cfg = &g_comms_i2c_codec_cfg, .p_api =
		&g_comms_on_comms_i2c, };
gpt_instance_ctrl_t g_timer_audio_mclk_ctrl;
#if 0
const gpt_extended_pwm_cfg_t g_timer_audio_mclk_pwm_extend =
{
    .trough_ipl             = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT2_COUNTER_UNDERFLOW)
    .trough_irq             = VECTOR_NUMBER_GPT2_COUNTER_UNDERFLOW,
#else
    .trough_irq             = FSP_INVALID_VECTOR,
#endif
    .poeg_link              = GPT_POEG_LINK_POEG0,
    .output_disable         = (gpt_output_disable_t) ( GPT_OUTPUT_DISABLE_NONE),
    .adc_trigger            = (gpt_adc_trigger_t) ( GPT_ADC_TRIGGER_NONE),
    .dead_time_count_up     = 0,
    .dead_time_count_down   = 0,
    .adc_a_compare_match    = 0,
    .adc_b_compare_match    = 0,
    .interrupt_skip_source  = GPT_INTERRUPT_SKIP_SOURCE_NONE,
    .interrupt_skip_count   = GPT_INTERRUPT_SKIP_COUNT_0,
    .interrupt_skip_adc     = GPT_INTERRUPT_SKIP_ADC_NONE,
    .gtioca_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
    .gtiocb_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
};
#endif
const gpt_extended_cfg_t g_timer_audio_mclk_extend =
		{ .gtioca = { .output_enabled = true, .stop_level = GPT_PIN_LEVEL_LOW },
				.gtiocb = { .output_enabled = false, .stop_level =
						GPT_PIN_LEVEL_LOW }, .start_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .stop_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .clear_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .count_up_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .count_down_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_a_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_b_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_a_ipl = (BSP_IRQ_DISABLED),
				.capture_b_ipl = (BSP_IRQ_DISABLED), .compare_match_c_ipl =
						(BSP_IRQ_DISABLED), .compare_match_d_ipl =
						(BSP_IRQ_DISABLED), .compare_match_e_ipl =
						(BSP_IRQ_DISABLED), .compare_match_f_ipl =
						(BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_A)
    .capture_a_irq         = VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_A,
#else
				.capture_a_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_B)
    .capture_b_irq         = VECTOR_NUMBER_GPT2_CAPTURE_COMPARE_B,
#else
				.capture_b_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT2_COMPARE_C)
    .compare_match_c_irq   = VECTOR_NUMBER_GPT2_COMPARE_C,
#else
				.compare_match_c_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT2_COMPARE_D)
    .compare_match_d_irq   = VECTOR_NUMBER_GPT2_COMPARE_D,
#else
				.compare_match_d_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT2_COMPARE_E)
    .compare_match_e_irq   = VECTOR_NUMBER_GPT2_COMPARE_E,
#else
				.compare_match_e_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT2_COMPARE_F)
    .compare_match_f_irq   = VECTOR_NUMBER_GPT2_COMPARE_F,
#else
				.compare_match_f_irq = FSP_INVALID_VECTOR,
#endif
				.compare_match_value = { (uint32_t) 0x0, /* CMP_A */
						(uint32_t) 0x0, /* CMP_B */(uint32_t) 0x0, /* CMP_C */
						(uint32_t) 0x0, /* CMP_D */(uint32_t) 0x0, /* CMP_E */
						(uint32_t) 0x0, /* CMP_F */}, .compare_match_status =
						((0U << 5U) | (0U << 4U) | (0U << 3U) | (0U << 2U)
								| (0U << 1U) | 0U), .capture_filter_gtioca =
						GPT_CAPTURE_FILTER_NONE, .capture_filter_gtiocb =
						GPT_CAPTURE_FILTER_NONE,
#if 0
    .p_pwm_cfg             = &g_timer_audio_mclk_pwm_extend,
#else
				.p_pwm_cfg = NULL,
#endif
#if 0
    .gtior_setting.gtior_b.gtioa  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.oadflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.oahld  = 0U,
    .gtior_setting.gtior_b.oae    = (uint32_t) true,
    .gtior_setting.gtior_b.oadf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfaen  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsa  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
    .gtior_setting.gtior_b.gtiob  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.obdflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.obhld  = 0U,
    .gtior_setting.gtior_b.obe    = (uint32_t) false,
    .gtior_setting.gtior_b.obdf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfben  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsb  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
#else
				.gtior_setting.gtior = 0U,
#endif

				.gtioca_polarity = GPT_GTIOC_POLARITY_NORMAL, .gtiocb_polarity =
						GPT_GTIOC_POLARITY_NORMAL, };

const timer_cfg_t g_timer_audio_mclk_cfg = { .mode = TIMER_MODE_PERIODIC,
/* Actual period: 8e-8 seconds. Actual duty: 50%. */.period_counts =
		(uint32_t) 0x14, .duty_cycle_counts = 0xa, .source_div =
		(timer_source_div_t) 0, .channel = 2, .p_callback = NULL,
/** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
		.p_context = (void*) &NULL,
#endif
		.p_extend = &g_timer_audio_mclk_extend, .cycle_end_ipl =
				(BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT2_COUNTER_OVERFLOW)
    .cycle_end_irq       = VECTOR_NUMBER_GPT2_COUNTER_OVERFLOW,
#else
		.cycle_end_irq = FSP_INVALID_VECTOR,
#endif
		};
/* Instance structure to use this module. */
const timer_instance_t g_timer_audio_mclk = {
		.p_ctrl = &g_timer_audio_mclk_ctrl, .p_cfg = &g_timer_audio_mclk_cfg,
		.p_api = &g_timer_on_gpt };
dtc_instance_ctrl_t g_transfer_i2s_tx_ctrl;

#if (1 == 1)
transfer_info_t g_transfer_i2s_tx_info DTC_TRANSFER_INFO_ALIGNMENT =
		{ .transfer_settings_word_b.dest_addr_mode = TRANSFER_ADDR_MODE_FIXED,
				.transfer_settings_word_b.repeat_area =
						TRANSFER_REPEAT_AREA_SOURCE,
				.transfer_settings_word_b.irq = TRANSFER_IRQ_END,
				.transfer_settings_word_b.chain_mode =
						TRANSFER_CHAIN_MODE_DISABLED,
				.transfer_settings_word_b.src_addr_mode =
						TRANSFER_ADDR_MODE_INCREMENTED,
				.transfer_settings_word_b.size = TRANSFER_SIZE_4_BYTE,
				.transfer_settings_word_b.mode = TRANSFER_MODE_BLOCK, .p_dest =
						(void*) NULL, .p_src = (void const*) NULL, .num_blocks =
						(uint16_t) 0, .length = (uint16_t) 0, };

#elif (1 > 1)
/* User is responsible to initialize the array. */
transfer_info_t g_transfer_i2s_tx_info[1] DTC_TRANSFER_INFO_ALIGNMENT;
#else
/* User must call api::reconfigure before enable DTC transfer. */
#endif

const dtc_extended_cfg_t g_transfer_i2s_tx_cfg_extend = { .activation_source =
		VECTOR_NUMBER_SSI0_TXI, };

const transfer_cfg_t g_transfer_i2s_tx_cfg = {
#if (1 == 1)
		.p_info = &g_transfer_i2s_tx_info,
#elif (1 > 1)
    .p_info              = g_transfer_i2s_tx_info,
#else
    .p_info = NULL,
#endif
		.p_extend = &g_transfer_i2s_tx_cfg_extend, };

/* Instance structure to use this module. */
const transfer_instance_t g_transfer_i2s_tx = { .p_ctrl =
		&g_transfer_i2s_tx_ctrl, .p_cfg = &g_transfer_i2s_tx_cfg, .p_api =
		&g_transfer_on_dtc };
ssi_instance_ctrl_t g_i2s_audio_ctrl;

/** SSI instance configuration */
const ssi_extended_cfg_t g_i2s_audio_cfg_extend = { .audio_clock =
		(ssi_audio_clock_t) SSI_AUDIO_CLOCK_INTERNAL, .bit_clock_div =
		SSI_CLOCK_DIV_24, };

/** I2S interface configuration */
const i2s_cfg_t g_i2s_audio_cfg = { .channel = 0, .pcm_width =
		I2S_PCM_WIDTH_16_BITS, .operating_mode = I2S_MODE_MASTER, .word_length =
		I2S_WORD_LENGTH_16_BITS, .ws_continue = I2S_WS_CONTINUE_OFF,
		.p_callback = audio_i2s_callback, .p_context = NULL, .p_extend =
				&g_i2s_audio_cfg_extend,
#if (2) != BSP_IRQ_DISABLED
		.txi_irq = VECTOR_NUMBER_SSI0_TXI,
#else
                .txi_irq                 = FSP_INVALID_VECTOR,
#endif
#if (BSP_IRQ_DISABLED) != BSP_IRQ_DISABLED
                .rxi_irq                 = VECTOR_NUMBER_SSI0_RXI,
#else
		.rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SSI0_INT)
                .int_irq                 = VECTOR_NUMBER_SSI0_INT,
#else
		.int_irq = FSP_INVALID_VECTOR,
#endif
		.txi_ipl = (2), .rxi_ipl = (BSP_IRQ_DISABLED), .idle_err_ipl = (2),
#define RA_NOT_DEFINED (1)
#if (RA_NOT_DEFINED == g_transfer_i2s_tx)
                .p_transfer_tx       = NULL,
#else
		.p_transfer_tx = &g_transfer_i2s_tx,
#endif
#if (RA_NOT_DEFINED == RA_NOT_DEFINED)
		.p_transfer_rx = NULL,
#else
                .p_transfer_rx       = &RA_NOT_DEFINED,
#endif
#undef RA_NOT_DEFINED
		};

/* Instance structure to use this module. */
const i2s_instance_t g_i2s_audio = { .p_ctrl = &g_i2s_audio_ctrl, .p_cfg =
		&g_i2s_audio_cfg, .p_api = &g_i2s_on_ssi };
/* I2C Communication Device */
rm_comms_i2c_instance_ctrl_t g_comms_i2c_device0_ctrl;

/* Lower level driver configuration */
const i2c_master_cfg_t g_comms_i2c_device0_lower_level_cfg = { .slave = 0x38,
		.addr_mode = I2C_MASTER_ADDR_MODE_7BIT, .p_callback =
				rm_comms_i2c_callback, };

const rm_comms_cfg_t g_comms_i2c_device0_cfg = {
		.semaphore_timeout = 0xFFFFFFFF, .p_lower_level_cfg =
				(void*) &g_comms_i2c_device0_lower_level_cfg, .p_extend =
				(void*) &g_comms_i2c_bus0_extended_cfg, .p_callback =
				comms_i2c_callback,
#if defined(NULL)
    .p_context          = NULL,
#else
		.p_context = (void*) &NULL,
#endif
		};

const rm_comms_instance_t g_comms_i2c_device0 = { .p_ctrl =
		&g_comms_i2c_device0_ctrl, .p_cfg = &g_comms_i2c_device0_cfg, .p_api =
		&g_comms_on_comms_i2c, };
gpt_instance_ctrl_t g_timer_camera_xclk_ctrl;
#if 0
const gpt_extended_pwm_cfg_t g_timer_camera_xclk_pwm_extend =
{
    .trough_ipl             = (BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT12_COUNTER_UNDERFLOW)
    .trough_irq             = VECTOR_NUMBER_GPT12_COUNTER_UNDERFLOW,
#else
    .trough_irq             = FSP_INVALID_VECTOR,
#endif
    .poeg_link              = GPT_POEG_LINK_POEG0,
    .output_disable         = (gpt_output_disable_t) ( GPT_OUTPUT_DISABLE_NONE),
    .adc_trigger            = (gpt_adc_trigger_t) ( GPT_ADC_TRIGGER_NONE),
    .dead_time_count_up     = 0,
    .dead_time_count_down   = 0,
    .adc_a_compare_match    = 0,
    .adc_b_compare_match    = 0,
    .interrupt_skip_source  = GPT_INTERRUPT_SKIP_SOURCE_NONE,
    .interrupt_skip_count   = GPT_INTERRUPT_SKIP_COUNT_0,
    .interrupt_skip_adc     = GPT_INTERRUPT_SKIP_ADC_NONE,
    .gtioca_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
    .gtiocb_disable_setting = GPT_GTIOC_DISABLE_PROHIBITED,
};
#endif
const gpt_extended_cfg_t g_timer_camera_xclk_extend =
		{ .gtioca = { .output_enabled = true, .stop_level = GPT_PIN_LEVEL_LOW },
				.gtiocb = { .output_enabled = false, .stop_level =
						GPT_PIN_LEVEL_LOW }, .start_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .stop_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .clear_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .count_up_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .count_down_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_a_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_b_source = (gpt_source_t)(
						GPT_SOURCE_NONE), .capture_a_ipl = (BSP_IRQ_DISABLED),
				.capture_b_ipl = (BSP_IRQ_DISABLED), .compare_match_c_ipl =
						(BSP_IRQ_DISABLED), .compare_match_d_ipl =
						(BSP_IRQ_DISABLED), .compare_match_e_ipl =
						(BSP_IRQ_DISABLED), .compare_match_f_ipl =
						(BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_A)
    .capture_a_irq         = VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_A,
#else
				.capture_a_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_B)
    .capture_b_irq         = VECTOR_NUMBER_GPT12_CAPTURE_COMPARE_B,
#else
				.capture_b_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT12_COMPARE_C)
    .compare_match_c_irq   = VECTOR_NUMBER_GPT12_COMPARE_C,
#else
				.compare_match_c_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT12_COMPARE_D)
    .compare_match_d_irq   = VECTOR_NUMBER_GPT12_COMPARE_D,
#else
				.compare_match_d_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT12_COMPARE_E)
    .compare_match_e_irq   = VECTOR_NUMBER_GPT12_COMPARE_E,
#else
				.compare_match_e_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_GPT12_COMPARE_F)
    .compare_match_f_irq   = VECTOR_NUMBER_GPT12_COMPARE_F,
#else
				.compare_match_f_irq = FSP_INVALID_VECTOR,
#endif
				.compare_match_value = { (uint32_t) 0x0, /* CMP_A */
						(uint32_t) 0x0, /* CMP_B */(uint32_t) 0x0, /* CMP_C */
						(uint32_t) 0x0, /* CMP_D */(uint32_t) 0x0, /* CMP_E */
						(uint32_t) 0x0, /* CMP_F */}, .compare_match_status =
						((0U << 5U) | (0U << 4U) | (0U << 3U) | (0U << 2U)
								| (0U << 1U) | 0U), .capture_filter_gtioca =
						GPT_CAPTURE_FILTER_NONE, .capture_filter_gtiocb =
						GPT_CAPTURE_FILTER_NONE,
#if 0
    .p_pwm_cfg             = &g_timer_camera_xclk_pwm_extend,
#else
				.p_pwm_cfg = NULL,
#endif
#if 0
    .gtior_setting.gtior_b.gtioa  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.oadflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.oahld  = 0U,
    .gtior_setting.gtior_b.oae    = (uint32_t) true,
    .gtior_setting.gtior_b.oadf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfaen  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsa  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
    .gtior_setting.gtior_b.gtiob  = (0U << 4U) | (0U << 2U) | (0U << 0U),
    .gtior_setting.gtior_b.obdflt = (uint32_t) GPT_PIN_LEVEL_LOW,
    .gtior_setting.gtior_b.obhld  = 0U,
    .gtior_setting.gtior_b.obe    = (uint32_t) false,
    .gtior_setting.gtior_b.obdf   = (uint32_t) GPT_GTIOC_DISABLE_PROHIBITED,
    .gtior_setting.gtior_b.nfben  = ((uint32_t) GPT_CAPTURE_FILTER_NONE & 1U),
    .gtior_setting.gtior_b.nfcsb  = ((uint32_t) GPT_CAPTURE_FILTER_NONE >> 1U),
#else
				.gtior_setting.gtior = 0U,
#endif

				.gtioca_polarity = GPT_GTIOC_POLARITY_NORMAL, .gtiocb_polarity =
						GPT_GTIOC_POLARITY_NORMAL, };

const timer_cfg_t g_timer_camera_xclk_cfg = { .mode = TIMER_MODE_PERIODIC,
/* Actual period: 4e-8 seconds. Actual duty: 50%. */.period_counts =
		(uint32_t) 0xa, .duty_cycle_counts = 0x5, .source_div =
		(timer_source_div_t) 0, .channel = 12, .p_callback = NULL,
/** If NULL then do not add & */
#if defined(NULL)
    .p_context           = NULL,
#else
		.p_context = (void*) &NULL,
#endif
		.p_extend = &g_timer_camera_xclk_extend, .cycle_end_ipl =
				(BSP_IRQ_DISABLED),
#if defined(VECTOR_NUMBER_GPT12_COUNTER_OVERFLOW)
    .cycle_end_irq       = VECTOR_NUMBER_GPT12_COUNTER_OVERFLOW,
#else
		.cycle_end_irq = FSP_INVALID_VECTOR,
#endif
		};
/* Instance structure to use this module. */
const timer_instance_t g_timer_camera_xclk = { .p_ctrl =
		&g_timer_camera_xclk_ctrl, .p_cfg = &g_timer_camera_xclk_cfg, .p_api =
		&g_timer_on_gpt };
iic_master_instance_ctrl_t g_i2c_master_camera_ctrl;
const iic_master_extended_cfg_t g_i2c_master_camera_extend =
		{ .timeout_mode = IIC_MASTER_TIMEOUT_MODE_SHORT, .timeout_scl_low =
				IIC_MASTER_TIMEOUT_SCL_LOW_ENABLED, .smbus_operation = 0,
				/* Actual calculated bitrate: 97809. Actual calculated duty cycle: 49%. */.clock_settings.brl_value =
						17, .clock_settings.brh_value = 16,
				.clock_settings.cks_value = 4, .clock_settings.sddl_value = 0,
				.clock_settings.dlcs_value = 0, };
const i2c_master_cfg_t g_i2c_master_camera_cfg = { .channel = 1, .rate =
		I2C_MASTER_RATE_STANDARD, .slave = 0x3C, .addr_mode =
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
		.p_callback = i2c_camera_callback, .p_context = NULL,
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
		.ipl = (4), .p_extend = &g_i2c_master_camera_extend, };
/* Instance structure to use this module. */
const i2c_master_instance_t g_i2c_master_camera = { .p_ctrl =
		&g_i2c_master_camera_ctrl, .p_cfg = &g_i2c_master_camera_cfg, .p_api =
		&g_i2c_master_on_iic };
sci_b_uart_instance_ctrl_t g_jlink_console_ctrl;

sci_b_baud_setting_t g_jlink_console_baud_setting = {
/* Baud rate calculated with 0.160% error. */.baudrate_bits_b.abcse = 0,
		.baudrate_bits_b.abcs = 0, .baudrate_bits_b.bgdm = 1,
		.baudrate_bits_b.cks = 0, .baudrate_bits_b.brr = 64,
		.baudrate_bits_b.mddr = (uint8_t) 256, .baudrate_bits_b.brme = false };

/** UART extended configuration for UARTonSCI HAL driver */
const sci_b_uart_extended_cfg_t g_jlink_console_cfg_extend = { .clock =
		SCI_B_UART_CLOCK_INT,
		.rx_edge_start = SCI_B_UART_START_BIT_FALLING_EDGE, .noise_cancel =
				SCI_B_UART_NOISE_CANCELLATION_DISABLE, .rx_fifo_trigger =
				SCI_B_UART_RX_FIFO_TRIGGER_MAX, .p_baud_setting =
				&g_jlink_console_baud_setting, .flow_control =
				SCI_B_UART_FLOW_CONTROL_RTS,
#if 0xFF != 0xFF
                .flow_control_pin       = BSP_IO_PORT_FF_PIN_0xFF,
                #else
		.flow_control_pin = (bsp_io_port_pin_t) UINT16_MAX,
#endif
		.rs485_setting = { .enable = SCI_B_UART_RS485_DISABLE, .polarity =
				SCI_B_UART_RS485_DE_POLARITY_HIGH, .assertion_time = 1,
				.negation_time = 1, } };

/** UART interface configuration */
const uart_cfg_t g_jlink_console_cfg = { .channel = 8, .data_bits =
		UART_DATA_BITS_8, .parity = UART_PARITY_OFF, .stop_bits =
		UART_STOP_BITS_1, .p_callback = jlink_console_callback, .p_context =
		NULL, .p_extend = &g_jlink_console_cfg_extend,
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
		.rxi_ipl = (4), .txi_ipl = (12), .tei_ipl = (12), .eri_ipl = (12),
#if defined(VECTOR_NUMBER_SCI8_RXI)
                .rxi_irq             = VECTOR_NUMBER_SCI8_RXI,
#else
		.rxi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_TXI)
                .txi_irq             = VECTOR_NUMBER_SCI8_TXI,
#else
		.txi_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_TEI)
                .tei_irq             = VECTOR_NUMBER_SCI8_TEI,
#else
		.tei_irq = FSP_INVALID_VECTOR,
#endif
#if defined(VECTOR_NUMBER_SCI8_ERI)
                .eri_irq             = VECTOR_NUMBER_SCI8_ERI,
#else
		.eri_irq = FSP_INVALID_VECTOR,
#endif
		};

/* Instance structure to use this module. */
const uart_instance_t g_jlink_console = { .p_ctrl = &g_jlink_console_ctrl,
		.p_cfg = &g_jlink_console_cfg, .p_api = &g_uart_on_sci_b };
void g_hal_init(void) {
	g_common_init();
}
