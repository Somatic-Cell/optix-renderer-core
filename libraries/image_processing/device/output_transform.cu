#include <cuda_runtime.h>
#include "output_encoding.cuh"

#include <cstdint>

static_assert(sizeof(uchar4) == 4);
static_assert(sizeof(cudaSurfaceObject_t) == sizeof(std::uint64_t));

extern "C" __global__ void renderer_output_transform_rgb8(
    // 読み込み元
    const unsigned char* source,
    std::uint64_t source_stride,
    // 書き込み先
    unsigned char* destination,
    std::uint64_t destination_stride,
    // 画像のサイズ
    std::uint32_t width,
    std::uint32_t height,
    float exposure_scale
)
{
    // どの画素を担当しているかを調べる
    const std::uint64_t x = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::uint64_t y = static_cast<std::uint64_t>(blockIdx.y) * blockDim.y + threadIdx.y;
    
    // 範囲外の画素を担当するスレッドは早期終了
    if(x >= width || y >= height)
    {
        return;
    }

    const renderer::RGB32f* input_row = reinterpret_cast<const renderer::RGB32f*>(
        source + y * source_stride
    );

    renderer::RGB8* output_row = reinterpret_cast<renderer::RGB8*>(
        destination + y * destination_stride
    );

    const renderer::RGB32f input = input_row[x];

    if(!isfinite(input.r) || !isfinite(input.g) || !isfinite(input.b))
    {
        output_row[x] = {0, 0, 0};
        return;
    }
    
    const float clip_limit = 1.0f / exposure_scale;

    output_row[x] = renderer::image_processing::device::encode_rgb8(input_row[x], exposure_scale);

}


extern "C" __global__ void renderer_output_transform_rgb8_surface(
    // 読み込み元
    const unsigned char* source,
    std::uint64_t source_stride,
    // 書き込み先 (CUDA surface object は書き込み先が RGBA で確定)
    cudaSurfaceObject_t surface,
    // 画像のサイズ
    std::uint32_t width,
    std::uint32_t height,
    float exposure_scale
)
{
    // どの画素を担当しているかを調べる
    const std::uint64_t x = static_cast<std::uint64_t>(blockIdx.x) * blockDim.x + threadIdx.x;
    const std::uint64_t y = static_cast<std::uint64_t>(blockIdx.y) * blockDim.y + threadIdx.y;
    
    // 範囲外の画素を担当するスレッドは早期終了
    if(x >= width || y >= height)
    {
        return;
    }

    const renderer::RGB32f* input_row = reinterpret_cast<const renderer::RGB32f*>(
        source + y * source_stride
    );

    const renderer::RGB8 rgb = renderer::image_processing::device::encode_rgb8(input_row[x], exposure_scale); 
    const uchar4 rgba = make_uchar4(rgb.r, rgb.g, rgb.b, 255);
    surf2Dwrite(rgba, surface, static_cast<int>(x * sizeof(uchar4)), static_cast<int>(y));
}