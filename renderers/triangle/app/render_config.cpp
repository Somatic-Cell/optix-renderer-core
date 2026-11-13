#include "render_config.hpp"
#include "render_config_json.hpp"

#include <renderer/config_io/json_document.hpp>


#include <stdexcept>
#include <string>

namespace renderer::config
{
namespace
{

std::filesystem::path absolute_config_path(const std::filesystem::path& path)
{
    if(path.empty())
    {
        throw std::invalid_argument("Configuration path must not be empty");
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::string path_text(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}


} // namespace

RenderConfig load_render_config(const std::filesystem::path& path)
{
    const auto config_path = absolute_config_path(path);
    const auto document = config_io::load_json_document(config_path);
    RenderConfig config;
    try
    {
        config = document.get<RenderConfig>();
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument(
            "Configuration '" + path_text(config_path) + "': " + error.what());
    }
    config.output.path = (config_path.parent_path() / config.output.path).lexically_normal();
    return config;
}

void save_render_config(const std::filesystem::path& path, const RenderConfig& config)
{
    const auto config_path = absolute_config_path(path);
    auto document = nlohmann::json(config);

    auto stored = config;
    const auto absolute_output = std::filesystem::absolute(config.output.path).lexically_normal();
    const auto relative_output = absolute_output.lexically_relative(config_path.parent_path());
    stored.output.path = relative_output.empty() ? absolute_output : relative_output;
    document = stored;
    config_io::save_json_document(config_path, document);
}

}
