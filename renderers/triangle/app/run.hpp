#pragma once

#include <renderer/triangle/config.hpp>

#include <filesystem>

namespace renderer::triangle
{

int run(
    const RenderConfig& config,
    const std::filesystem::path& module_directory
);

} // namespace renderer::triangle
