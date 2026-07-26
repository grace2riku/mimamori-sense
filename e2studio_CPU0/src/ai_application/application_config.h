/**
 * @file application_config.h
 * @brief Application-level configuration for AI demo and memory allocation
 * @details
 * Defines the active AI demo type, memory allocation strategy, and
 * feature enablement flags.
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/application_config.h
 */
#ifndef APPLICATION_CONFIG_H__
#define APPLICATION_CONFIG_H__

// ###################### DEFINE AI DEMO #######################
#define FACE_DETECTION                      1
#define IMAGE_CLASSIFICATION                2
#define FALL_DETECTION                      3

#define AI_DEMO                             (FALL_DETECTION)

// ###################### MEMORY ALLOCATION ######################
/* Defines for memory allocation options */
#define ALLOCATE_TO_ONCHIP_ROM              0
#define ALLOCATE_TO_ONCHIP_RAM              1
#define ALLOCATE_TO_SDRAM                   2 // Buffer will be located in ".sdram"
#define ALLOCATE_TO_SDRAM_INITIAL_IN_OSPI   3 // Buffer will be located in ".sdram_ospi_data.data"
#define ALLOCATE_TO_OSPI                    4 // Buffer will be located in ".ospi_device_1.data"

/* Selection of target memory space for application buffer */
#define CAMERA_CAPTURE_BUFFER_ALLOCATION    ALLOCATE_TO_SDRAM           /* Option: OnchipRAM or SDRAM */
#define CAMERA_TEMPORARY_BUFFER_ALLOCATION  ALLOCATE_TO_ONCHIP_RAM      /* Option: OnchipRAM or SDRAM */
#define CAMERA_IMAGE_ALLOCATION             ALLOCATE_TO_ONCHIP_RAM      /* Option: OnchipRAM or SDRAM */

#define AI_INPUT_IMAGE_ALLOCATION           ALLOCATE_TO_ONCHIP_RAM      /* Option: OnchipRAM or SDRAM */
#define AI_MODEL_ALLOCATION                 ALLOCATE_TO_ONCHIP_ROM      /* Option: OnchipROM, OnchipRAM, SDRAM(IntialOSPI) or OSPI */
#define TENSOR_ARENA_ALLOCATION             ALLOCATE_TO_ONCHIP_RAM      /* Option: OnchipRAM or SDRAM */

// ################## FUNCTION ENABLEMENT SETTING ################
#define ENABLE_CAMERA_INPUT                          (1) // 0: Disabled, 1: Enabled
#define ENABLE_LCD_DISPLAY_OUTPUT                    (1) // 0: Disabled, 1: Enabled

/* ENABLE_INFERENCE_RUNNING_LED は LED1(P600) を推論の実行中インジケータとして
 * ON/OFF する（common_util.h: INFERENCE_START/END_INDICATE_LED →
 * ai_inference_thread_entry.c の推論ループ内で使用）。
 * しかし P600 は usermain.c の blink_task が 500ms ごとにトグルする
 * μT-Kernel 生存確認用 LED と同一ピンであり、有効にすると両者が同じ GPIO を
 * 奪い合って blink_task の点滅周期が観測できなくなる（#175）。
 * 推論の実行状況は NT-Shell の `ai status`（Inference count）で確認できるため
 * 既定は無効とする。オシロで推論周期・実行時間を観測したいときだけ (1) に戻すこと
 * （その間は LED1 の点滅による生存確認ができなくなる点に注意）。 */
#define ENABLE_INFERENCE_RUNNING_LED                 (0) // 0: Disabled, 1: Enabled
/* LED2(P303) は CPU1(FreeRTOS) の blinky が点滅させるため、こちらも既定は無効。 */
#define ENABLE_CAMERA_CAPTURE_RUNNING_LED            (0) // 0: Disabled, 1: Enabled

#define ENABLE_CONSOLE_OUTPUT_SCREEN_CLEAR           (1) // 0: Disabled, 1: Enabled. If you'd like to keep a log data, set 0 (disabled).
#define ENABLE_AI_INFERENCE_RESULT_CONSOLE_OUTPUT    (1) // 0: Disabled, 1: Enabled
#define ENABLE_PROCESSING_TIME_RESULT_CONSOLE_OUTPUT (1) // 0: Disabled, 1: Enabled

// ------------------ Internal auto config ------------------
#if ((AI_MODEL_ALLOCATION) == (ALLOCATE_TO_OSPI))
#define REQUIRE_OSPI_OPEN
#elif ((AI_MODEL_ALLOCATION) == (ALLOCATE_TO_SDRAM_INITIAL_IN_OSPI))
#define REQUIRE_OSPI_OPEN
#define REQUIRE_OSPI_MEMORY_COPY_TO_SDRAM
#endif

#endif /* APPLICATION_CONFIG_H__ */
