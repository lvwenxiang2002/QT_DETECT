#include "InferKernels.h"

namespace hpc_vision {

// 融合 Kernel
__global__ void fused_preprocess_kernel(
    const uint8_t* __restrict__ src,
    int src_width, int src_height,
    int crop_x, int crop_y, int crop_size, int overlap,
    float* __restrict__ dst, int dst_size,
    int light_field, // 0: Backlight (Odd lines, pad 255), 1: DarkField (Even lines, pad 0)
    int pad_value,
    int channels
) {
    int dx = blockIdx.x * blockDim.x + threadIdx.x;
    int dy = blockIdx.y * blockDim.y + threadIdx.y;

    if (dx >= dst_size || dy >= dst_size) return;

    // 映射回裁切图 (1280x1280) 中的坐标 (浮点，当尺寸相同时插值退化为一一对应)
    float scale = (float)crop_size / (float)dst_size;
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
        if (x < 0 || x >= src_width || y < 0 || y >= split_height) {
            return (float)pad_value;
        }
        // 根据硬件实际输出，背光 (light_field=0) 在偶数行，暗场 (light_field=1) 在奇数行
        int mapped_y = y * 2 + (light_field == 0 ? 0 : 1);
        return (float)src[mapped_y * src_width + x];
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
    for (int c = 0; c < channels; ++c) {
        dst[c * (dst_size * dst_size) + dy * dst_size + dx] = val;
    }
}

void launch_fused_preprocess_kernel(
    const uint8_t* d_src_interleaved,
    int src_width, int src_height,
    int crop_x, int crop_y, int crop_size, int overlap,
    float* d_dst_float, int dst_size,
    int light_field,
    cudaStream_t stream,
    int channels
) {
    int pad_value = (light_field == 0) ? 255 : 0;

    dim3 block(16, 16);
    dim3 grid((dst_size + block.x - 1) / block.x, (dst_size + block.y - 1) / block.y);

    fused_preprocess_kernel<<<grid, block, 0, stream>>>(
        d_src_interleaved,
        src_width, src_height,
        crop_x, crop_y, crop_size, overlap,
        d_dst_float, dst_size,
        light_field,
        pad_value,
        channels
    );
}

} // namespace hpc_vision
