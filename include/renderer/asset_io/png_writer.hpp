#pragma once

#include <renderer/core/image.hpp>

#include <filesystem>

namespace renderer::asset_io
{

// fpng を使って PNG ファイルをエンコードし，保存する
// 既存のファイルは上書きされる
void write_png(
    const std::filesystem::path& path,  // 保存先のパス
    const SRGB8Image& image             // エンコードしたい画像
);

} //namespace renderer::asset_io