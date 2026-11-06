#pragma once 

#include <cstdint>
#include <filesystem>

namespace renderer::config
{

enum class RendererType {
    triangle
};

// 出力画像のフォーマット
enum class OutputFormat {
    png
};

// 色空間
enum class ColorEncoding {
    sRGB
};

// トーンマップの方法
enum class ToneMapping {
    clip
};

struct DeviceSettings
{
    int ordinal = 0;
    bool optix_validation = false;
}

// 
struct RendererSettings
{
    RendererType type = RendererType::triangle;
    std::uint32_t width = 800;
    std::uint32_t height = 600;
};

struct OutputSettings
{
    std::filesystem::path path = "triangle.png";
    OutputFormat format = OutputFormat::png;
    ColorEncoding color_encoding = ColorEncoding::sRGB;
    ToneMapping tone_mapping = ToneMapping::clip;
    float exposure_ev = 0.0f;
};

struct DiagnosticsSettings
{
    bool verify_triangle = false;
};

struct  RenderConfig
{
    DeviceSettings device;
    RendererSettings renderer;
    OutputSettings output;
    DiagnosticsSettings diagnostics;
};

[[nodiscard]] RenderConfig load_render_config(
    const std::filesystem::path& path
);

} // namespace renderer::config