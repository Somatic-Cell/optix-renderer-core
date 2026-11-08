#include <renderer/core/pixel.hpp>

#include <cmath>
#include <cstdint>

namespace 
{

__device__ std::uint8_t encode_channel(
    float linear,
    float exposure_scale,
    float clip_limit
)
{
    // 範囲外を clamp
    linear = fminf(fmaxf(linear, 0.0f), clip_limit);
    linear = fminf(linear * exposure_scale, 1.0f);
    const float encoded = linear <= 0.0031308f
        ? 12.92f * linear
        : 1.055f * powf(linear, 1.0 / 2.4) - 0.055;
    const float bounded = fminf(fmaxf(encoded, 0.0f), 1.0f);
    return static_cast<std::uint8_t>(std::roundf(bounded * 255.0f));
}

} // namespace 

extern "C" __global__ void renderer_linear_to_srgb8(
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

    output_row[x] = {
        encode_channel(input.r, exposure_scale, clip_limit),
        encode_channel(input.g, exposure_scale, clip_limit),
        encode_channel(input.b, exposure_scale, clip_limit)
    };


}