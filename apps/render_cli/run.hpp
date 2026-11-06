#pragma once

#include <renderer/config/render_config.hpp>

#include <filesystem>

namespace renderer::app
{

int run(
    const config::RenderConfig& config,
    const std::filesystem::path& module_directory
);

} // namespace renderer::app