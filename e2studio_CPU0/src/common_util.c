/**
 * @file common_util.c
 * @brief Common utility global variable definitions
 * @details
 * Defines global variables declared as extern in common_util.h.
 * These include AI detection results, classification results,
 * and processing time measurements.
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/common_util.h (extern declarations)
 */

/**********************************************************************************************************************
 Includes   <System Includes> , "Project Includes"
 *********************************************************************************************************************/
#include "common_util.h"

/**********************************************************************************************************************
 Exported global variables
 *********************************************************************************************************************/

/** AI detection results (bounding boxes) */
st_ai_detection_point_t g_ai_detection[AI_MAX_DETECTION_NUM];

/** AI classification results */
st_ai_classification_point_t g_ai_classification[AI_MAX_DETECTION_NUM];

/** Processing time measurements */
processing_time_info_t g_processing_time;

/**********************************************************************************************************************
 Exported global functions
 *********************************************************************************************************************/

/**
 * Handle application error by indicating via LED and halting
 *
 * @param err Error code
 */
void handle_error(vision_ai_app_err_t err)
{
    (void)err;
    ERROR_INDICATE;
}
