#pragma once

#include <filesystem>
#include <string_view>

#if defined(_WIN32)
#define RENDERER_CLI_ENTRY_POINT wmain
#else
#define RENDERER_CLI_ENTRY_POINT main
#endif

namespace renderer::app_support
{
using CliAction = int (*) (
    const std::filesystem::path& config_path,
    const std::filesystem::path& module_directory
);

using ViewerAction = int (*)(
    const std::filesystem::path& module_directory
);

int run_cli(
    int argc,
    const std::filesystem::path::value_type* const* argv,
    std::string_view executable_name,
    std::u8string_view default_module_directory,
    CliAction action
);

int run_viewer_cli(
    int argc,
    const std::filesystem::path::value_type* const* argv,
    std::string_view executable_name,
    std::u8string_view default_module_directory,
    ViewerAction action
);

} // namespace renderer::app_support