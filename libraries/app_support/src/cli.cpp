#include <renderer/app_support/cli.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace renderer::app_support
{
namespace 
{

struct Options
{
    std::filesystem::path config_path;
    std::filesystem::path module_directory;
    bool help = false;
};

Options parse_options(
    int argc, 
    const std::filesystem::path::value_type* const* argv
)
{
    Options options;

    // コマンドライン引数を頭から読む
    for(int i = 1; i < argc; ++i)
    {
        const std::filesystem::path argument(argv[i]);

        // ヘルプだった場合
        if(argument == "--help" || argument == "-h")
        {
            options.help = true;
            continue;
        }

        // --config または --module を読む
        // フィールドを探す
        const bool is_config = argument == "--config";
        
        // 想定外の引数
        if(!is_config && argument != "--modules")
        {
            throw std::invalid_argument("Unknown command-line option; use --help");
        }

        // フィールドに何が設定されたかを読む
        // --config または --module の後ろには値があるはず
        if(++i == argc)
        {
            throw std::invalid_argument("Missing value for --config or --modules");
        }

        // フィールドに設定された値
        const std::filesystem::path value(argv[i]);

        if(value.empty())
        {
            throw std::invalid_argument("Command-line paths must not be empty");
        }

        auto& destination = is_config ? options.config_path : options.module_directory;

        if(!destination.empty())
        {
            throw std::invalid_argument(
                is_config   ? "--config may be specified only once" 
                            : "--modules may be specified only once"
            );
        }

        destination = value;

    }
    // config は必須
    if (!options.help && options.config_path.empty())
    {
        throw std::invalid_argument("--config is requrired; use --help");
    }
    return options;
}

} // namespace 

int run_cli(
    int argc, 
    const::std::filesystem::path::value_type* const* argv,
    std::string_view executable_name,
    std::u8string_view default_module_directory,
    CliAction action
)
{
    try
    {
        const auto options = parse_options(argc, argv);

        // help のメッセージ
        if(options.help)
        {
            std::cout   << "Usage: " << executable_name << " --config FILE [--modules DIRECTORY]\n"
                        << "Json output paths are relative to the configuration file.\n"
                        << "--modules overrides the module directory selected by CMake.\n"
                        << "Command-line paths are relative to the working directory.\n";
            return EXIT_SUCCESS;
        }

        const auto directory = options.module_directory.empty()
                    ? std::filesystem::path(default_module_directory) : options.module_directory;
        return action(options.config_path, std::filesystem::absolute(directory).lexically_normal());
    }
    catch(const std::exception& error)
    {
        std::cerr << executable_name << ": " << error.what() << "\n";
        return EXIT_FAILURE;
    }

}

} // namespace renderer::app_support