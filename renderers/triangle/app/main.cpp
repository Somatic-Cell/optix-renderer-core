#include "run.hpp"

#include <renderer/app_support/cli.hpp>
#include <renderer/triangle/config.hpp>

#include <filesystem>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must be supplied by the build system
#endif

int RENDERER_CLI_ENTRY_POINT(int argc, std::filesystem::path::value_type** argv)
{
    return renderer::app_support::run_cli(argc, argv, "triangle_cli", u8"" RENDERER_MODULE_DIRECTORY,
        [](const std::filesystem::path& config_path, const std::filesystem::path& module_directory)
        {
            return renderer::triangle::run(renderer::triangle::load_render_config(config_path), module_directory);
        });
}
