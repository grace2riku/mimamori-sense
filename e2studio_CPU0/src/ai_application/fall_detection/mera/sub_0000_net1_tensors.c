#include "sub_0000_net1_tensors.h"

const TensorInfo sub_0000_net1_tensors[] = {
  { "_split_1_command_stream", 0, 27160, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 1, 315280, "MODEL", 0xffffffff },
  { "_split_1_scratch", 2, 991872, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 3, 991872, "FAST_SCRATCH", 0x0 },
  { "serving_default_images_0", 6, 110592, "INPUT_TENSOR", 0x24000 },
  { "model_78_tf_reshape_23_Reshape_70431", 5, 3024, "OUTPUT_TENSOR", 0x5e80 },
  { "model_78_tf_math_sigmoid_56_Sigmoid1_70442", 4, 756, "OUTPUT_TENSOR", 0x300 },
};

const size_t sub_0000_net1_tensors_count = sizeof(sub_0000_net1_tensors) / sizeof(sub_0000_net1_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0000_net1_address_serving_default_images_0 = 0x24000;
const uint32_t sub_0000_net1_address_model_78_tf_reshape_23_Reshape_70431 = 0x5e80;
const uint32_t sub_0000_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442 = 0x300;

