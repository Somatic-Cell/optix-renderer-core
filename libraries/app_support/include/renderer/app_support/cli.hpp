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

// action には利用者が指定したパスと，絶対パスを解決したモジュールのディレクトリを渡す
int run_cli(
    int argc,
    const std::filesystem::path::value_type* const* argv,
    std::string_view executable_name,
    std::u8string_view default_module_director,
    CliAction action
);

} // namespace renderer::app_support