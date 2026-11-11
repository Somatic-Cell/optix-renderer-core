#pragma once 

#include <renderer/output/output_settings.hpp>

#include <cstdint>
#include <filesystem>

namespace renderer::config
{

enum class RendererType {
    triangle
};

struct DeviceSettings
{
    int ordinal = 0;
    bool optix_validation = false;
};

// 
struct RendererSettings
{
    RendererType type = RendererType::triangle;
    std::uint32_t width = 800;
    std::uint32_t height = 600;
};


struct DiagnosticsSettings
{
    bool verify_triangle = false;
};

struct RenderConfig
{
    DeviceSettings device;
    RendererSettings renderer;
    output::OutputSettings output{.path = "triangle.png"};
    DiagnosticsSettings diagnostics;
};

[[nodiscard]] RenderConfig load_render_config(
    const std::filesystem::path& path
);

void save_render_config(
    const std::filesystem::path& path,
    const RenderConfig& config
);

} // namespace renderer::config