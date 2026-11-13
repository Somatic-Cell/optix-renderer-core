#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace renderer 
{
[[nodiscard]]
inline std::size_t checked_row_bytes(
    std::uint32_t width,
    std::size_t pixel_size
)
{
    if (width == 0 || pixel_size == 0)
    {
        throw std::invalid_argument("Image width and pixel size must be positive");
    }

    if (pixel_size > (std::numeric_limits<std::size_t>::max)() / width)
    {
        throw std::length_error("Image row byte size overflows size_t");
    }
    return static_cast<std::size_t>(width * pixel_size);
}

[[nodiscard]]
std::size_t required_image_bytes(
    std::uint32_t width,        // 画像の幅
    std::uint32_t height,       // 画像の高さ
    std::size_t stride_bytes,   // 画像 1 行分のサイズ
    std::size_t pixel_size      // 画素数
)
{
    // 不正な値でないかどうかをチェック
    // 縦横の幅
    if (height == 0) {
        throw std::invalid_argument("Image height must be positive");
    }

    // 1 行分に必要な画素数がストライドのバイトサイズに収まっているかどうか
    const auto row_bytes = checked_row_bytes(width, pixel_size);
    if(stride_bytes < row_bytes)
    {
        throw std::invalid_argument("GPU image row stride is too small");
    }

    // 画像の最後の行から 1 行前までに必要なバイトサイズ
    const auto prefix = checked_row_bytes(height - 1, stride_bytes);
    if(prefix > std::numeric_limits<std::size_t>::max() - row_bytes)
    {
        throw std::length_error("GPU image byte size overflows size_t");
    }

    // 最後の行は row_bytes 分だけあればよい (末尾の stride 用の調整が不要)
    return prefix + row_bytes;
}

[[nodiscard]]
inline std::size_t packed_image_bytes(
    std::uint32_t width,
    std::uint32_t height,
    std::size_t pixel_size
)
{
    return required_image_bytes(width, height, checked_row_bytes(width, pixel_size), pixel_size);
}

} // namespace renderer