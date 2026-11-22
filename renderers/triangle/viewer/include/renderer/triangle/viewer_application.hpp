#pragma once

#include <renderer/render_viewer/application.hpp>

#include <filesystem>

namespace renderer::triangle
{

[[nodiscard]] render_viewer::Application make_viewer_application(
    const std::filesystem::path& module_directory
);

} // namespace renderer::triangle
