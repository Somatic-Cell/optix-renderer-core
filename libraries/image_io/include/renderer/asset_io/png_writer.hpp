#pragma once

#include <renderer/core/image.hpp>

#include <filesystem>

namespace renderer::asset_io
{

// ｋの RGB8 PNG writer が使えるサイズと，fpng の内部バッファの上限を検査する
void validate_png_dimensions(std::uint32_t width, std::uint32_t height);

// fpng を使って PNG ファイルをエンコードし，保存する
// 既存のファイルは上書きされる
void write_png(
    const std::filesystem::path& path,  // 保存先のパス
    const SRGB8ImageView image             // エンコードしたい画像
);

void write_png(
    const std::filesystem::path& path,  // 保存先のパス
    const SRGB8Image& image             // エンコードしたい画像
);

} //namespace renderer::asset_io