#include "render_config.hpp"
#include <renderer/image_processing/output_transform_settings.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace renderer::config
{
namespace
{

using Json = nlohmann::json;

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

void require_choice(
    const Json& value,
    std::string_view field,
    std::string_view supported
)
{
    if (get_string_value(value, field) != supported)
    {
        throw_invalid(field, "unsupported value; expected '" + std::string(supported) + "'");
    }
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
    if(const Json* renderer = get_member(document, "renderer"))
    {
        require_members(*renderer,
            {
                "type",
                "width",
                "height"
            },
            "renderer"
        );

        if
    }
}
}
}
