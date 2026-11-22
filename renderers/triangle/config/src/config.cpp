#include <renderer/triangle/config.hpp>
#include <renderer/app_support/config_json.hpp>
#include <renderer/config_io/json_fields.hpp>

#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace renderer::triangle
{
namespace
{

constexpr auto renderer_fields = [](auto& fields, auto& value)
{
    // 省略時は triangle とみなす．明示されていれば，この実行ファイル用の設定か照合する．
    constexpr std::string_view renderer_name = "triangle";
    std::string type(renderer_name);
    fields.field("type", type);
    if (type != renderer_name)
    {
        throw std::invalid_argument("type: unsupported value '" + type + "'");
    }
    fields.field("width", value.width);
    fields.field("height", value.height);
};

constexpr auto diagnostics_fields = [](auto& fields, auto& value)
{
    fields.field("verify_triangle", value.verify_triangle);
};

constexpr auto config_fields = [](auto& fields, auto& value)
{
    app_support::bind_schema_version(fields);
    fields.field("device", value.device);
    fields.field("renderer", value.renderer);
    fields.field("output", value.output, app_support::OutputSettingsCodec{});
    fields.field("diagnostics", value.diagnostics);
};

void validate_renderer(const RendererSettings& value)
{
    if (value.width == 0 || value.height == 0)
    {
        throw std::invalid_argument("width and height must be positive");
    }
}

void validate_config(const RenderConfig& value)
{
    try
    {
        image_export::validate_export_request(value.output, value.renderer.width, value.renderer.height);
    }
    catch (const std::length_error& error)
    {
        throw std::invalid_argument(error.what());
    }
    if (value.diagnostics.verify_triangle && (value.renderer.width != 800 || value.renderer.height != 600))
    {
        throw std::invalid_argument("diagnostics.verify_triangle requires an 800x600 image");
    }
}

} // namespace

void from_json(const nlohmann::json& document, RendererSettings& value)
{
    auto candidate = value;
    config_io::read_json_fields(document, candidate, renderer_fields);
    validate_renderer(candidate);
    value = candidate;
}

void to_json(nlohmann::json& document, const RendererSettings& value)
{
    validate_renderer(value);
    document = config_io::write_json_fields(value, renderer_fields);
}

void from_json(const nlohmann::json& document, DiagnosticsSettings& value)
{
    config_io::read_json_fields(document, value, diagnostics_fields);
}

void to_json(nlohmann::json& document, const DiagnosticsSettings& value)
{
    document = config_io::write_json_fields(value, diagnostics_fields);
}

void from_json(const nlohmann::json& document, RenderConfig& value)
{
    // 完全な設定文書は毎回既定値から読み込み，すべての検証に成功してから反映する．
    RenderConfig candidate{};
    config_io::read_json_fields(document, candidate, config_fields);
    validate_config(candidate);
    value = std::move(candidate);
}

void to_json(nlohmann::json& document, const RenderConfig& value)
{
    validate_config(value);
    document = config_io::write_json_fields(value, config_fields);
}

RenderConfig load_render_config(const std::filesystem::path& path)
{
    return app_support::load_config_file<RenderConfig>(path);
}

void save_render_config(const std::filesystem::path& path, const RenderConfig& config)
{
    app_support::save_config_file(path, config);
}

} // namespace renderer::triangle
