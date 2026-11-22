#pragma once

#include <renderer/app_support/device_settings.hpp>
#include <renderer/image_export/export_settings.hpp>
#include <nlohmann/json_fwd.hpp>

#include <cstdint>
#include <filesystem>

namespace renderer::triangle
{

struct RendererSettings
{
    std::uint32_t width = 800;
    std::uint32_t height = 600;
};

struct DiagnosticsSettings
{
    bool verify_triangle = false;
};

struct RenderConfig
{
    app_support::DeviceSettings device;
    RendererSettings renderer;
    image_export::ExportSettings output{.path = "triangle.png"};
    DiagnosticsSettings diagnostics;
};

void from_json(const nlohmann::json& document, RendererSettings& value);
void to_json(nlohmann::json& document, const RendererSettings& value);
void from_json(const nlohmann::json& document, DiagnosticsSettings& value);
void to_json(nlohmann::json& document, const DiagnosticsSettings& value);
void from_json(const nlohmann::json& document, RenderConfig& value);
void to_json(nlohmann::json& document, const RenderConfig& value);

[[nodiscard]] RenderConfig load_render_config(const std::filesystem::path& path);
void save_render_config(const std::filesystem::path& path, const RenderConfig& config);

} // namespace renderer::triangle
