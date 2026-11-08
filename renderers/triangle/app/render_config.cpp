#include "render_config.hpp"
#include <renderer/asset_io/png_writer.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace renderer::config
{
namespace
{

using Json = nlohmann::json;

constexpr std::array renderer_types{
    std::pair{std::string_view{"triangle"}, RendererType::triangle}
};
constexpr std::array output_formats{
    std::pair{std::string_view{"png"}, output::OutputFormat::png}
};
constexpr std::array tone_mappings{
    std::pair{std::string_view{"clip"}, image_processing::ToneMapping::clip}
};
constexpr std::array color_encodings{
    std::pair{std::string_view{"srgb"}, image_processing::ColorEncoding::sRGB}
};

std::string path_text(const std::filesystem::path& path)
{
    const auto text = path.generic_u8string();
    return std::string(text.begin(), text.end());
}

[[noreturn]] void throw_invalid(
    std::string_view field, 
    std::string_view reason
)
{
    throw std::invalid_argument(std::string(field) + ": " + std::string(reason));
}

void require_members(
    const Json& object,                                 // Json
    std::initializer_list<std::string_view> allowed,    // 期待される key
    std::string_view field                              
)
{
    if (!object.is_object())
    {
        throw_invalid(field, "must be an object");
    }
    for (auto item = object.begin(); item != object.end(); ++item)
    {
        if(std::find(allowed.begin(), allowed.end(), item.key()) == allowed.end())
        {
            throw_invalid(field, "unknown member '" + item.key() + "'");
        }
    }
}

const Json* get_member(
    const Json& object,     // 検索する json object
    std::string_view name   // 検索する key
)
{
    const auto found = object.find(std::string(name));
    return found == object.end() ? nullptr : &found.value();
}

// [minimum, maximum] の範囲にある uint64 を取得する
std::uint64_t get_unsigned_integer(
    const Json& value,
    std::string_view field,
    std::uint64_t minimum,
    std::uint64_t maximum
)
{
    std::uint64_t result = 0;
    // 型のチェック
    // uint の場合，受け入れる
    if (value.is_number_unsigned())
    {
        result = value.get<std::uint64_t>();
    }
    // int の場合，非負なら受け入れる
    else if (value.is_number_integer())
    {
        const auto signed_value = value.get<std::int64_t>();
        if(signed_value < 0)
        {
            throw_invalid(field, "must be a non-negative integer");
        }
        result = static_cast<std::uint64_t>(signed_value);
    }
    // それ以外の型は受け入れない
    else 
    {
        throw_invalid(field, "must be an integer");
    }

    // 範囲内かどうかチェック
    if (result < minimum || result > maximum)
    {
        throw_invalid(field, "must be in [" + std::to_string(minimum) + ", " + std::to_string(maximum) + "]");
    }

    return result;
}

bool get_boolean(const Json& value, std::string_view field)
{
    if(!value.is_boolean())
    {
        throw_invalid(field, "must be a boolean");
    }
    return value.get<bool>();
}

std::string get_string_value(const Json& value, std::string_view field)
{
    if(!value.is_string())
    {
        throw_invalid(field, "must be a string");
    }
    return value.get<std::string>();
}

template<class Enum, std::size_t Count>
Enum get_enum(
    const Json& value,
    std::string_view field,
    const std::array<std::pair<std::string_view, Enum>, Count>& choices)
{
    const std::string name = get_string_value(value, field);
    for (const auto& [spelling, result] : choices)
    {
        if (name == spelling)
        {
            return result;
        }
    }

    std::string expected;
    for (const auto& choice : choices)
    {
        if (!expected.empty())
        {
            expected += ", ";
        }
        expected += "'";
        expected += choice.first;
        expected += "'";
    }
    throw_invalid(field, "unsupported value '" + name + "'; expected " + expected);
}

float get_exposure(const Json& value)
{
    constexpr std::string_view field = "output.exposure_ev";
    if(!value.is_number())
    {
        throw_invalid(field, "must be a number");
    }

    const double result = value.get<double>();
    
    try
    {
        image_processing::validate_exposure_ev(result);
    }
    catch(const std::invalid_argument& error)
    {
        throw_invalid("output", error.what());
    }

    return static_cast<float>(result);
}

std::filesystem::path get_output_path(const Json& value)
{
    constexpr std::string_view field = "output.path";
    const std::string text = get_string_value(value, field);
    if (text.find('\0') != std::string::npos)
    {
        throw_invalid(field, "must not contain a NUL character");
    }
    const std::filesystem::path result(std::u8string(text.begin(), text.end()));
    if (result.has_root_name() && !result.has_root_directory())
    {
        throw_invalid(field,
            "drive-relative paths are not supported; use an absolute path "
            "or a path relative to the configuration file");
    }
    return result;
}

RenderConfig decode(const Json& document)
{
    require_members(document,
        {
            "schema_version",
            "device",
            "renderer",
            "output",
            "diagnostics",
        },
        "configuration"
    );

    // バージョンの確認
    const Json* version = get_member(document, "schema_version");
    if(version == nullptr)
    {
        throw_invalid("schema_version", "is_required");
    }

    static_cast<void>(get_unsigned_integer(*version, "schema_version", 1, 1));

    RenderConfig config;
    if(const Json* device = get_member(document, "device"))
    {
        require_members(*device, 
            {
                "ordinal",
                "optix_validation",
            },
            "device"
        );

        if(const Json* value = get_member(*device, "optix_validation"))
        {
            config.device.optix_validation = get_boolean(*value, "device.optix.validation");
        }
    }

    // レンダラの設定の取得
    if (const Json* renderer = get_member(document, "renderer"))
    {
        require_members(*renderer, {"type", "width", "height"}, "renderer");
        if (const Json* value = get_member(*renderer, "type"))
        {
            config.renderer.type = get_enum(*value, "renderer.type", renderer_types);
        }
        if (const Json* value = get_member(*renderer, "width"))
        {
            config.renderer.width = static_cast<std::uint32_t>(get_unsigned_integer(
                *value, "renderer.width", 1, (std::numeric_limits<std::uint32_t>::max)()));
        }
        if (const Json* value = get_member(*renderer, "height"))
        {
            config.renderer.height = static_cast<std::uint32_t>(get_unsigned_integer(
                *value, "renderer.height", 1, (std::numeric_limits<std::uint32_t>::max)()));
        }
    }

    if (const Json* settings = get_member(document, "output"))
    {
        require_members(*settings,
            {"path", "format", "color_encoding", "tone_mapping", "exposure_ev"},
            "output");
        if (const Json* value = get_member(*settings, "path"))
        {
            config.output.path = get_output_path(*value);
        }
        if (const Json* value = get_member(*settings, "format"))
        {
            config.output.format = get_enum(*value, "output.format", output_formats);
        }
        if (const Json* value = get_member(*settings, "color_encoding"))
        {
            config.output.transform.color_encoding =
                get_enum(*value, "output.color_encoding", color_encodings);
        }
        if (const Json* value = get_member(*settings, "tone_mapping"))
        {
            config.output.transform.tone_mapping =
                get_enum(*value, "output.tone_mapping", tone_mappings);
        }
        if (const Json* value = get_member(*settings, "exposure_ev"))
        {
            config.output.transform.exposure_ev = get_exposure(*value);
        }
    }

    if (const Json* diagnostics = get_member(document, "diagnostics"))
    {
        require_members(*diagnostics, {"verify_triangle"}, "diagnostics");
        if (const Json* value = get_member(*diagnostics, "verify_triangle"))
        {
            config.diagnostics.verify_triangle = get_boolean(*value, "diagnostics.verify_triangle");
        }
    }

    try
    {
        output::validate_output_settings(config.output);
        asset_io::validate_png_dimensions(config.renderer.width, config.renderer.height);
    }
    catch (const std::invalid_argument& error)
    {
        throw_invalid("output", error.what());
    }
    catch (const std::length_error& error)
    {
        throw_invalid("output", error.what());
    }
    if (config.diagnostics.verify_triangle
        && (config.renderer.width != 800 || config.renderer.height != 600))
    {
        throw_invalid("diagnostics.verify_triangle", "requires an 800 x 600 image");
    }
    return config;
}

Json parse_document(std::istream& input)
{
    // DOM に格納されると重複したキーが上書きされるため，読み込み中に検査する．
    std::vector<std::set<std::string>> object_keys;
    const auto check_keys = [&](int, Json::parse_event_t event, Json& parsed)
    {
        switch (event)
        {
        case Json::parse_event_t::object_start:
            object_keys.emplace_back();
            break;
        case Json::parse_event_t::key:
            if (!object_keys.back().insert(parsed.get<std::string>()).second)
            {
                throw_invalid("configuration", "duplicate member '" + parsed.get<std::string>() + "'");
            }
            break;
        case Json::parse_event_t::object_end:
            object_keys.pop_back();
            break;
        default:
            break;
        }
        return true;
    };
    return Json::parse(input, check_keys);
}

} // namespace

RenderConfig load_render_config(const std::filesystem::path& path)
{
    if (path.empty())
    {
        throw std::invalid_argument("Configuration path must not be empty");
    }
    const auto config_path = std::filesystem::absolute(path).lexically_normal();
    std::ifstream input(config_path, std::ios::binary);
    if (!input)
    {
        throw std::runtime_error("Cannot open configuration file: " + path_text(config_path));
    }

    RenderConfig config;
    try
    {
        config = decode(parse_document(input));
    }
    catch (const Json::exception& error)
    {
        throw std::invalid_argument(
            "Invalid JSON in configuration '" + path_text(config_path) + "': " + error.what());
    }
    catch (const std::invalid_argument& error)
    {
        throw std::invalid_argument(
            "Configuration '" + path_text(config_path) + "': " + error.what());
    }
    if (input.bad())
    {
        throw std::runtime_error("Failed to read configuration file: " + path_text(config_path));
    }

    config.output.path = (config_path.parent_path() / config.output.path).lexically_normal();
    return config;
}

}
