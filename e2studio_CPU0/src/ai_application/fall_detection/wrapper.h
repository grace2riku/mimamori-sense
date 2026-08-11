/**
 * @file wrapper.h
 * @brief MERA model wrapper for fall detection (YOLO-Fastest V1)
 * @details
 * Wraps the MERA auto-generated model functions (model_net1.h) with
 * application-friendly names, following the same pattern as the
 * face detection sample.
 *
 * YOLO-Fastest V1 model (darknet-based, anchor-based):
 *   Input:  224x224x3 RGB INT8 (150528 bytes)
 *   Output: 2 branches
 *     - StatefulPartitionedCall_0: 7x7x18 = 882 bytes (branch 0, stride 32)
 *     - StatefulPartitionedCall_1: 14x14x18 = 3528 bytes (branch 1, stride 16)
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
 * YOLO_FASTEST_MODEL: Set to 1 for YOLO-Fastest V1 (single NPU subgraph).
 */
#define YOLO_FASTEST_MODEL  (1)

/**
 * Get pointer to the model input buffer
 *
 * @return Pointer to the INT8 input buffer (224x224x3 = 150528 bytes)
 */
static inline int8_t* mera_input_ptr(void) {
    return GetModelInputPtr_net1_serving_default_image_input_0();
}

/**
 * Get pointer to branch 0 output tensor (7x7 grid, stride 32)
 *
 * MERA output: StatefulPartitionedCall_0 = 7x7x3x6 = 882 bytes
 *
 * @return Pointer to INT8 output tensor (648 bytes)
 */
static inline int8_t* mera_output_ptr_branch0(void) {
    /* 末尾の _NNNNN は MERA 生成時の内部ノードID。モデル再変換で変わるため
       mera/model_net1.h の関数名と一致させること (Issue #144 で _70535→_70327)。 */
    return GetModelOutputPtr_net1_StatefulPartitionedCall_0_70327();
}

/**
 * Get pointer to branch 1 output tensor (14x14 grid, stride 16)
 *
 * MERA output: StatefulPartitionedCall_1 = 14x14x3x6 = 3528 bytes
 *
 * @return Pointer to INT8 output tensor (2592 bytes)
 */
static inline int8_t* mera_output_ptr_branch1(void) {
    /* 末尾の _NNNNN は MERA 生成時の内部ノードID。モデル再変換で変わるため
       mera/model_net1.h の関数名と一致させること (Issue #144 で _70554→_70338)。 */
    return GetModelOutputPtr_net1_StatefulPartitionedCall_1_70338();
}

/**
 * Get the model arena size for subgraph 0
 *
 * YOLO-Fastest V1 uses a single NPU subgraph (sub_0000 only).
 * Arena size: 432 KiB (442368 bytes)
 */
static inline uint32_t mera_arena_size_sub0(void) {
    return (uint32_t)kArenaSize_sub_0000_net1;
}

/**
 * Invoke NPU inference via Ethos-U55
 *
 * @details Runs the model on the data previously copied into the
 *          input buffer (mera_input_ptr()). Blocks until complete.
 *
 * Reference: mera/model_net1.h - RunModel_net1()
 */
static inline void mera_invoke(void) {
    RunModel_net1(false);
}

#endif /* FALL_DETECTION_WRAPPER_H */
