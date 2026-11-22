#pragma once

#include <renderer/image_processing/output_transform_settings.hpp>
#include <renderer/image_types/image.hpp>

#include <cstdint>
#include <filesystem>

namespace renderer::image_export
{

// 出力画像のフォーマット
enum class ImageFileFormat {
    png
};

struct ExportSettings
{
    std::filesystem::path path;
    ImageFileFormat format = ImageFileFormat::png;
    image_processing::OutputTransformSettings transform{};
};

void validate_export_path(const std::filesystem::path& path);

void validate_export_settings(const ExportSettings& settings);

// 画素データを用意する前に，出力する形式で要求する画像の寸法を扱えるかどうか確認する
void validate_export_dimensions(ImageFileFormat format, std::uint32_t width, std::uint32_t height);

void validate_export_request(
    const ExportSettings& settings,
    std::uint32_t width,
    std::uint32_t height
);

void write_export_image(
    const ExportSettings& settings,
    RGB8ImageView image
);

} // namespace renderer::image_export