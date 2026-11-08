#pragma once

#include <renderer/image_processing/output_transform_settings.hpp>

#include <filesystem>
#include <stdexcept>

namespace renderer::output
{

// 出力画像のフォーマット
enum class OutputFormat {
    png
};

struct OutputSettings
{
    std::filesystem::path path;
    OutputFormat format = OutputFormat::png;
    image_processing::OutputTransformSettings transform{};
};

inline void validate_output_settings(const OutputSettings& settings)
{
    if (settings.path.empty())
    {
        throw std::invalid_argument("Output path must not be empty");
    }

    if (settings.format != OutputFormat::png)
    {
        throw std::invalid_argument("Unsupported output format");
    }

    image_processing::validate_output_transform_settings(settings.transform);
}

inline void validate_output_settings(const OutputSettings& settings)
{
    if(settings.path.empty())
    {
        throw std::invalid_argument("Output must not be empty");
    }

    if (settings.format != OutputFormat::png)
    {
        throw std::invalid_argument("Unsupported output format");
    }

    image_processing::validate_output_transform_settings(settings.transform);
}

} // namespace renderer::output