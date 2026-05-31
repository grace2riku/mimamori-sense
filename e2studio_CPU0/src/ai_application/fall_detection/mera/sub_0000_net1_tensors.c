#include "sub_0000_net1_tensors.h"

const TensorInfo sub_0000_net1_tensors[] = {
  { "_split_1_command_stream", 2, 12620, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 3, 434128, "MODEL", 0xffffffff },
  { "_split_1_scratch", 4, 442368, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 5, 442368, "FAST_SCRATCH", 0x0 },
  { "serving_default_image_input_0", 6, 110592, "INPUT_TENSOR", 0x24000 },
  { "StatefulPartitionedCall_1_70554", 1, 2592, "OUTPUT_TENSOR", 0x5580 },
  { "StatefulPartitionedCall_0_70535", 0, 648, "OUTPUT_TENSOR", 0xd80 },
};

const size_t sub_0000_net1_tensors_count = sizeof(sub_0000_net1_tensors) / sizeof(sub_0000_net1_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0000_net1_address_serving_default_image_input_0 = 0x24000;
const uint32_t sub_0000_net1_address_StatefulPartitionedCall_1_70554 = 0x5580;
const uint32_t sub_0000_net1_address_StatefulPartitionedCall_0_70535 = 0xd80;

