#include "sub_0000_net1_tensors.h"

const TensorInfo sub_0000_net1_tensors[] = {
  { "_split_1_command_stream", 2, 12600, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 3, 433712, "MODEL", 0xffffffff },
  { "_split_1_scratch", 4, 602112, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 5, 602112, "FAST_SCRATCH", 0x0 },
  { "serving_default_image_input_0", 6, 150528, "INPUT_TENSOR", 0x31000 },
  { "StatefulPartitionedCall_1_70338", 1, 3528, "OUTPUT_TENSOR", 0x7460 },
  { "StatefulPartitionedCall_0_70327", 0, 882, "OUTPUT_TENSOR", 0x1260 },
};

const size_t sub_0000_net1_tensors_count = sizeof(sub_0000_net1_tensors) / sizeof(sub_0000_net1_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0000_net1_address_serving_default_image_input_0 = 0x31000;
const uint32_t sub_0000_net1_address_StatefulPartitionedCall_1_70338 = 0x7460;
const uint32_t sub_0000_net1_address_StatefulPartitionedCall_0_70327 = 0x1260;

