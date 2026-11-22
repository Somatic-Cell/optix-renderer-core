#pragma once

#include <renderer/image_types/pixel.hpp>
#include <cmath>
#include <cstdint>

namespace renderer::image_processing::device
{

__device__ inline std::uint8_t encode_srgb_channel(
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
        : __fmul_rn(1.055f, powf(linear, 1.0f / 2.4f)) - 0.055f;
    const float bounded = fminf(fmaxf(encoded, 0.0f), 1.0f);
    return static_cast<std::uint8_t>(std::roundf(bounded * 255.0f));
}

__device__ inline RGB8 encode_rgb8(RGB32f input, float scale)
{
    if(!isfinite(input.r) || !isfinite(input.g) || !isfinite(input.b)) return {0, 0, 0};
    const float limit = 1.0f / scale;
    return {
        encode_srgb_channel(input.r, scale, limit),
        encode_srgb_channel(input.g, scale, limit),
        encode_srgb_channel(input.b, scale, limit)
    };
}

} // namespace renderer::image_processing::device

