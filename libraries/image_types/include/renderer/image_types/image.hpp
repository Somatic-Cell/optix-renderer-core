#pragma once

#include <renderer/image_types/pixel.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace renderer
{

// CPU 上の画像用バッファ
template<class Pixel>
struct Image
{
    static_assert(std::is_trivially_copyable_v<Pixel>);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<Pixel> pixels;
};


template<class Pixel>
struct ImageView
{
    static_assert(std::is_trivially_copyable_v<Pixel>);
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::span<const std::byte> bytes;
};

using RGB8Image = Image<RGB8>;
using RGB32fImage = Image<RGB32f>;
using RGB8ImageView = ImageView<RGB8>;
using RGB32fImageView = ImageView<RGB32f>;

} // namespace renderer