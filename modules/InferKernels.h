#pragma once

#include <cuda_runtime.h>
#include <stdint.h>

namespace hpc_vision {

// 极致优化：端到端融合 Kernel，执行：
// 1. 奇偶行分离 (光场提取)
// 2. 切图裁切 (20像素Overlap)
// 3. 边界 0 / 255 Padding
// 4. 双线性插值缩放到 1280x1280
// 5. 除以 255.0f 归一化并转成 FP32 NCHW 供 TensorRT 消费
// 
// 参数说明：
// d_src_interleaved: 原始交错大图数据 (单通道，uchar)
// src_width: 原图宽
// src_height: 原图高
// crop_x: 裁切起点 X
// crop_y: 裁切起点 Y (这是逻辑上的Y，对应拆分后的完整图像的Y。由于是大图交错，实际大图Y = crop_y * 2 + offset)
// crop_size: 裁切尺寸 (通常 2048)
// overlap: 重叠像素 (通常 20)
// d_dst_float: 目标显存，TensorRT 输入 Buffer (float, 尺寸 1*1*1280*1280)
// dst_size: 缩放目标尺寸 (通常 1280)
// light_field: 0-背光 (提取奇数行, 边缘用255填充), 1-暗场 (提取偶数行, 边缘用0填充)
// stream: 绑定的 CUDA Stream
void launch_fused_preprocess_kernel(
    const uint8_t* d_src_interleaved,
    int src_width, int src_height,
    int crop_x, int crop_y, int crop_size, int overlap,
    float* d_dst_float, int dst_size,
    int light_field,
    cudaStream_t stream,
    int channels = 1
);

} // namespace hpc_vision
