#pragma once

#include <cstdint>
#include <type_traits>

namespace renderer
{

// Linear RGB 
struct RGB32f
{
    float r;
    float g;
    float b;
};

// sRGB
struct RGB8
{
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
};

// 型チェック
static_assert(sizeof(RGB32f) == 3 * sizeof(float));
static_assert(std::is_trivially_copyable_v<RGB32f>);    // 値をバイト列のコピーで扱える
static_assert(std::is_standard_layout_v<RGB32f>);

static_assert(sizeof(RGB8) == 3);
static_assert(std::is_trivially_copyable_v<RGB8>);
static_assert(std::is_standard_layout_v<RGB8>);

} // namespace renderer