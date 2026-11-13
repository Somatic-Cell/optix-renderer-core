#pragma once

#include <renderer/core/pixel.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace renderer
{

// CPU 上の画像用バッファ
struct LinearRGBImage
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<RGB32f> pixels;
};

struct SRGB8Image
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<RGB8> pixels;
};

struct SRGB8ImageView
{
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::byte> bytes;
};

} // namespace renderer