#include "sub_0002_net1_tensors.h"

const TensorInfo sub_0002_net1_tensors[] = {
  { "_split_1_command_stream", 1, 1064, "COMMAND_STREAM", 0xffffffff },
  { "_split_1_flash", 2, 3824, "MODEL", 0xffffffff },
  { "_split_1_scratch", 3, 8400, "ARENA", 0x0 },
  { "_split_1_scratch_fast", 4, 8400, "FAST_SCRATCH", 0x0 },
  { "model_78_tf_strided_slice_1_StridedSlice_70443", 6, 1512, "INPUT_TENSOR", 0x1ae0 },
  { "model_78_tf_strided_slice_StridedSlice_70445", 7, 1512, "INPUT_TENSOR", 0xf00 },
  { "model_78_tf_math_sigmoid_56_Sigmoid1_70442", 5, 756, "INPUT_TENSOR", 0xc00 },
  { "PartitionedCall_0_70452", 0, 3780, "OUTPUT_TENSOR", 0xf00 },
};

const size_t sub_0002_net1_tensors_count = sizeof(sub_0002_net1_tensors) / sizeof(sub_0002_net1_tensors[0]);

// Addresses for each input and output buffer inside of the arena
const uint32_t sub_0002_net1_address_model_78_tf_strided_slice_1_StridedSlice_70443 = 0x1ae0;
const uint32_t sub_0002_net1_address_model_78_tf_strided_slice_StridedSlice_70445 = 0xf00;
const uint32_t sub_0002_net1_address_model_78_tf_math_sigmoid_56_Sigmoid1_70442 = 0xc00;
const uint32_t sub_0002_net1_address_PartitionedCall_0_70452 = 0xf00;

