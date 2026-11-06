#include "run.hpp"

#include <renderer/config/render_config.hpp>

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <iostream>
#include <stdexcept>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must be supplied by the build system
#endif

namespace
{

struct Options
{
    std::filesystem::path config_path;
    std::filesystem::path module_directory =
        std::filesystem::path(u8"" RENDERER_MODULE_DIRECTORY);
    bool help = false;
};

template<class Char>
Options parse_options(int argc, Char** argv)
{
    Options options;
    bool config_given = false;
    bool modules_given = false;
    for (int i = 1; i < argc; ++i)
    {
        const std::filesystem::path argument(argv[i]);
        if (argument == "--help" || argument == "-h")
        {
            options.help = true;
            continue;
        }
        if (argument != "--config" && argument != "--modules")
        {
            throw std::invalid_argument("Unknown command-line option; use --help");
        }
        if (++i == argc)
        {
            throw std::invalid_argument("Missing value for --config or --modules");
        }
        const std::filesystem::path value(argv[i]);
        if (value.empty())
        {
            throw std::invalid_argument("Command-line paths must not be empty");
        }
        if (argument == "--config")
        {
            if (config_given)
            {
                throw std::invalid_argument("--config may be specified only once");
            }
            config_given = true;
            options.config_path = value;
        }
        else
        {
            if (modules_given)
            {
                throw std::invalid_argument("--modules may be specified only once");
            }
            modules_given = true;
            options.module_directory = value;
        }
    }
    if (!options.help && !config_given)
    {
        throw std::invalid_argument("--config is required; use --help");
    }
    return options;
}

template<class Char>
int run_cli(int argc, Char** argv)
{
    try
    {
        const Options options = parse_options(argc, argv);
        if (options.help)
        {
            std::cout
                << "Usage: render_cli --config FILE [--modules DIRECTORY]\n"
                << "JSON output paths are relative to the configuration file.\n"
                << "--modules overrides the module directory selected by CMake.\n"
                << "Command-line paths are relative to the working directory.\n";
            return EXIT_SUCCESS;
        }
        const auto config = renderer::config::load_render_config(options.config_path);
        return renderer::app::run(config,
            std::filesystem::absolute(options.module_directory).lexically_normal());
    }
    catch (const std::exception& error)
    {
        std::cerr << "render_cli: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

} // namespace

#if defined(_WIN32)
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    return run_cli(argc, argv);
}
