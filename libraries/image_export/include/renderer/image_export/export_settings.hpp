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

inline void validate_export_settings(const ExportSettings& settings);

inline void validate_export_request(
    const ExportSettings& settings,
    std::uint32_t width,
    std::uint32_t height
);

void write_export_image(
    const ExportSettings& settings,
    RGB8ImageView image
);

} // namespace renderer::image_export