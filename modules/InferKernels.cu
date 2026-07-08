#include "InferKernels.h"

namespace hpc_vision {

// 融合 Kernel
__global__ void fused_preprocess_kernel(
    const uint8_t* __restrict__ src,
    int src_width, int src_height,
    int crop_x, int crop_y, int crop_size, int overlap,
    float* __restrict__ dst, int dst_width, int dst_height,
    int light_field, // 0: Backlight (Odd lines, pad 255), 1: DarkField (Even lines, pad 0)
    int pad_value,
    int channels
) {
    int dx = blockIdx.x * blockDim.x + threadIdx.x;
    int dy = blockIdx.y * blockDim.y + threadIdx.y;

    if (dx >= dst_width || dy >= dst_height) return;

    // 映射回裁切图 (1280x1280) 中的坐标 (默认 width 映射)
    float scale = (float)crop_size / (float)dst_width;
    float cx = dx * scale;
    float cy = dy * scale;

    // 逻辑坐标 -> 绝对坐标 (相对于拆解后的独立光场图)
    float abs_x = crop_x + cx;
    float abs_y = crop_y + cy;

    int x0 = (int)floorf(abs_x);
    int y0 = (int)floorf(abs_y);
    int x1 = x0 + 1;
    int y1 = y0 + 1;

    // 插值权重
    float wx = abs_x - x0;
    float wy = abs_y - y0;

    int split_height = src_height / 2;

    auto get_pixel = [&](int x, int y) -> float {
        // 使用边缘像素重复 (Replicate Padding) 替代常量 Padding，防止由于填充 255 导致的假性横向划伤/边缘过检
        int clamp_x = max(0, min(x, src_width - 1));
        int clamp_y = max(0, min(y, split_height - 1));
        int mapped_y = clamp_y * 2 + (light_field == 0 ? 0 : 1);
        return (float)src[mapped_y * src_width + clamp_x];
    };

    float p00 = get_pixel(x0, y0);
    float p10 = get_pixel(x1, y0);
    float p01 = get_pixel(x0, y1);
    float p11 = get_pixel(x1, y1);

    // 双线性插值
    float val_y0 = p00 * (1.0f - wx) + p10 * wx;
    float val_y1 = p01 * (1.0f - wx) + p11 * wx;
    float val = val_y0 * (1.0f - wy) + val_y1 * wy;

    // 归一化
    val = val / 255.0f;

    // 写入 NCHW 内存
    // NCHW layout
    if (channels == 3) {
        dst[0 * dst_height * dst_width + dy * dst_width + dx] = val; // R
        dst[1 * dst_height * dst_width + dy * dst_width + dx] = val; // G
        dst[2 * dst_height * dst_width + dy * dst_width + dx] = val; // B
    } else {
        dst[dy * dst_width + dx] = val;
    }
}

void launch_fused_preprocess_kernel(
    const uint8_t* d_src_interleaved,
    int src_width, int src_height,
    int crop_x, int crop_y, int crop_size, int overlap,
    float* d_dst_float, int dst_width, int dst_height,
    int light_field,
    cudaStream_t stream,
    int channels
) {
    dim3 block(16, 16);
    dim3 grid((dst_width + block.x - 1) / block.x,
              (dst_height + block.y - 1) / block.y);

    int pad_value = (light_field == 0) ? 255 : 0;

    fused_preprocess_kernel<<<grid, block, 0, stream>>>(
        d_src_interleaved,
        src_width, src_height,
        crop_x, crop_y, crop_size, overlap,
        d_dst_float, dst_width, dst_height,
        light_field,
        pad_value,
        channels
    );
}

} // namespace hpc_vision
