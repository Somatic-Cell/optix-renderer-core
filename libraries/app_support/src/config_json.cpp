#include <renderer/app_support/config_json.hpp>
#include <renderer/config_io/json_fields.hpp>

#include <array>
#include <string_view>
#include <utility>

namespace renderer::app_support
{
namespace 
{

using Json = nlohmann::json;
namespace io = renderer::config_io;

constexpr std::array output_formats{
    std::pair{std::string_view{"png"}, image_export::ImageFileFormat::png}
};
constexpr std::array tone_mappings{
    std::pair{std::string_view{"clip"}, image_processing::ToneMapping::clip}
};
constexpr std::array color_encodings{
    std::pair{std::string_view{"srgb"}, image_processing::TransferFunction::sRGB}
};

void validate_output_path(const std::filesystem::path& path)
{
    image_export::validate_export_path(path);
    if(path.has_root_name() && !path.has_root_directory())
    {
        throw std::invalid_argument("drive-relative paths are not supported");
    }
}

struct PathCodec
{
    void read(const Json& document, std::filesystem::path& value) const 
    {
        std::string text;
        io::JsonValueCodec{}.read(document, text);
        
        std::filesystem::path candidate(std::u8string(text.begin(), text.end()));
        
        validate_output_path(candidate);
        value = std::move(candidate);
    }

    Json write(const std::filesystem::path& value) const 
    {
        validate_output_path(value);
        const auto text = value.generic_u8string();
        return std::string(text.begin(), text.end());
    }
};

struct ExposureCodec
{
    void read(const Json& document, image_processing::ExposureEv& value) const 
    {
        if (!document.is_number())
        {
            throw std::invalid_argument("must be a number");
        }
        value = image_processing::ExposureEv{document.get<double>()};
    }

    Json write(image_processing::ExposureEv value) const 
    {
        return value.value();
    }
};

constexpr auto device_fields = [](auto& fields, auto& value)
{
    fields.field("ordinal", value.ordinal);
    fields.field("optix_validation", value.optix_validation);
};

constexpr auto output_fields = [](auto& fields, auto& value)
{
    fields.field("path", value.path, PathCodec{});
    fields.field("format", value.format, io::JsonEnumCodec{output_formats});
    fields.field("color_encoding", value.transform.transfer_function, io::JsonEnumCodec{color_encodings});
    fields.field("tone_mapping", value.transform.tone_mapping, io::JsonEnumCodec{tone_mappings});
    fields.field("exposure_ev", value.transform.exposure_ev, ExposureCodec{});
};

void validate_device(const DeviceSettings& value)
{
    // GPU を絶対に使う想定
    if(value.ordinal < 0)
    {
        throw std::invalid_argument("ordinal must not be negative");
    }
}

} // namespace 

// Device Settings
void from_json(const Json& document, DeviceSettings& value)
{
    auto candidate = value;
    io::read_json_fields_in_place(document, candidate, device_fields);
    validate_device(candidate);
    value = candidate;
}

void to_json(Json& document, const DeviceSettings& value)
{
    validate_device(value);
    document = io::write_json_fields(value, device_fields);
}

void OutputSettingsCodec::read(const nlohmann::json& document, image_export::ExportSettings& value) const
{
    auto candidate = value;
    io::read_json_fields_in_place(document, candidate, output_fields);

    // 部分読み込みでは，省略されたフィールドの元の値も検査してから反映する
    image_export::validate_export_settings(candidate);
    value = std::move(candidate);
}

nlohmann::json OutputSettingsCodec::write(const image_export::ExportSettings& value) const
{
    return config_io::write_json_fields(value, output_fields);
}

namespace detail
{

std::filesystem::path absolute_config_path(const std::filesystem::path& path)
{
    if (path.empty())
    {
        throw std::invalid_argument("Configuration path must not be empty");
    }
    return std::filesystem::absolute(path).lexically_normal();
}

std::filesystem::path output_path_for_save(
    const std::filesystem::path& config_directory, const std::filesystem::path& output)
{
    validate_output_path(output);
    const auto absolute = std::filesystem::absolute(output).lexically_normal();
    const auto relative = absolute.lexically_relative(config_directory);
    // ドライブが異なる場合は相対化できないので，絶対パスを保存する
    return relative.empty() ? absolute : relative;
}

} // namespace detail


} // namespace renderer::app_support