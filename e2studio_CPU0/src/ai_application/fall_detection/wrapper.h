/**
 * @file wrapper.h
 * @brief MERA model wrapper for fall detection (YOLOv8 pico INT8)
 * @details
 * Wraps the MERA auto-generated model functions (model_net1.h) with
 * application-friendly names, following the same pattern as the
 * face detection sample.
 *
 * MERA auto-generated functions (from F-003-4):
 *   - GetModelInputPtr_net1_serving_default_images_0()  : input buffer pointer
 *   - GetModelOutputPtr_net1_PartitionedCall_0_70452()   : output tensor pointer
 *   - RunModel_net1()                                    : invoke NPU inference
 *
 * Reference: reference_projects/ruhmi-framework-mcu/application_examples/
 *            face_detection/src/ai_application/face_detection/wrapper.h
 */
#ifndef FALL_DETECTION_WRAPPER_H
#define FALL_DETECTION_WRAPPER_H

#include "mera/model_net1.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * Get pointer to the model input buffer
 *
 * @return Pointer to the INT8 input buffer (192x192x3 = 110592 bytes)
 *
 * Reference: mera/model_net1.h - GetModelInputPtr_net1_serving_default_images_0()
 */
static inline int8_t* mera_input_ptr(void) {
    return GetModelInputPtr_net1_serving_default_images_0();
}

/**
 * Get pointer to the model output tensor
 *
 * YOLOv8 single output tensor layout (INT8, 3780 bytes):
 *   [5, 756] where rows = [x_center, y_center, w, h, class_score]
 *   756 candidates = P3(24x24=576) + P4(12x12=144) + P5(6x6=36)
 *
 * @return Pointer to the INT8 output tensor (5 x 756 = 3780 bytes)
 *
 * Reference: mera/model_net1.h - GetModelOutputPtr_net1_PartitionedCall_0_70452()
 */
static inline int8_t* mera_output_ptr(void) {
    return GetModelOutputPtr_net1_PartitionedCall_0_70452();
}

/**
 * Invoke NPU inference via Ethos-U55
 *
 * @details Runs the fall detection model on the data previously
 *          copied into the input buffer (mera_input_ptr()).
 *          This call blocks until inference is complete.
 *
 * Reference: mera/model_net1.h - RunModel_net1()
 */
static inline void mera_invoke(void) {
    RunModel_net1(false);
}

/**
 * Get the model arena sizes for diagnostics
 *
 * Reference: mera/sub_0000_net1_tensors.h, mera/sub_0002_net1_tensors.h
 */
static inline uint32_t mera_arena_size_sub0(void) {
    return (uint32_t)kArenaSize_sub_0000_net1;
}

static inline uint32_t mera_arena_size_sub2(void) {
    return (uint32_t)kArenaSize_sub_0002_net1;
}

#endif /* FALL_DETECTION_WRAPPER_H */
