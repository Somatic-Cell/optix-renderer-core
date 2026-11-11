#include "render_config_json.hpp"

#include <renderer/asset_io/png_writer.hpp>
#include <renderer/config_io/json_fields.hpp>

#include <array>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace renderer::config
{
namespace 
{

using Json = nlohmann::json;
namespace io = renderer::config_io;

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

struct VersionCodec
{
    void read(const Json& document, std::uint32_t& version) const 
    {
        io::JsonValueCodec{}.read(document, version);
        if(version != 1)
        {
            throw std::invalid_argument("unsupported schema version; expected 1");
        }
    }

    Json write(std::uint32_t version) const 
    {
            if(version != 1)
        {
            throw std::invalid_argument("unsupported schema version; expected 1");
        }
        return version;
    }
};

struct ExposureCodec
{
    void read(const Json& document, float& value) const 
    {
        double number = 0.0;
        io::JsonValueCodec{}.read(document, number);
        image_processing::validate_exposure_ev(number);
        value = static_cast<float>(number);
    }

    Json write(float value) const 
    {
        image_processing::validate_exposure_ev(value);
        return value;
    }
};

struct PathCodec
{
    static void validate(const std::filesystem::path& path)
    {
        if(
            path.native().find(std::filesystem::path::value_type{})
            != std::filesystem::path::string_type::npos
        )
        {
            throw std::invalid_argument("must not contain a NUL character");
        }

        if(path.has_root_name() && !path.has_root_directory())
        {
            throw std::invalid_argument("drive-relative paths are not supported");
        }
    }

    void read(const Json& document, std::filesystem::path& value) const 
    {
        std::string text;
        io::JsonValueCodec{}.read(document, text);
        
        std::filesystem::path candidate(std::u8string(text.begin(), text.end()));
        
        validate(candidate);
        value = std::move(candidate);
    }

    Json write(const std::filesystem::path& value) const 
    {
        validate(value);
        const auto text = value.generic_u8string();
        return std::string(text.begin(), text.end());
    }
};

constexpr auto device_fields = [](auto& fields, auto& value)
{
    fields.field("ordinal", value.ordinal);
    fields.field("optix_validation", value.optix_validation);
};

constexpr auto renderer_fields = [](auto& fields, auto& value)
{
    fields.field("type", value.type, io::JsonEnumCodec{renderer_types});
    fields.field("width", value.width);
    fields.field("height", value.height);
};

constexpr auto diagnostics_fields = [](auto& fields, auto& value)
{
    fields.field("verify_triangle", value.verify_triangle);
};

constexpr auto output_fields = [](auto& fields, auto& value)
{
    fields.field("path", value.path, PathCodec{});
    fields.field("format", value.format, io::JsonEnumCodec{output_formats});
    fields.field("color_encoding", value.transform.color_encoding, io::JsonEnumCodec{color_encodings});
    fields.field("tone_mapping", value.transform.tone_mapping, io::JsonEnumCodec{tone_mappings});
    fields.field("exposure_ev", value.transform.exposure_ev, ExposureCodec{});
};

struct OutputSettingsCodec
{
    void read(const Json& document, output::OutputSettings& value) const 
    {
        auto candidate = value;
        io::read_json_fields(document, candidate, output_fields);
        output::validate_output_settings(candidate);
        value = std::move(candidate);
    }

    Json write(const output::OutputSettings& value)
    {
        output::validate_output_settings(value);
        return io::write_json_fields(value, output_fields);
    }
};

constexpr auto config_fields = [](auto& fields, auto& value)
{
    std::uint32_t version = 1;
    fields.required("schema_version", version, VersionCodec{});
    fields.field("device", value.device);
    fields.field("renderer", value.renderer);
    fields.field("output", value.output, OutputSettingsCodec{});
    fields.field("diagnostics", value.diagnostics);
};


void validate_device(const DeviceSettings& value)
{
    // GPU を絶対に使う想定
    if(value.ordinal < 0)
    {
        throw std::invalid_argument("ordinal must not be negative");
    }
}

void validate_renderer(const RendererSettings& value)
{
    if(value.width == 0 || value.height == 0)
    {
        throw std::invalid_argument("width and height must be positive");
    }
}

void validate_config(const RenderConfig& value)
{
    validate_device(value.device);
    validate_renderer(value.renderer);
    output::validate_output_settings(value.output);

    try
    {
        asset_io::validate_png_dimensions(value.renderer.width, value.renderer.height);
    }
    catch(const std::exception& error)
    {
        throw std::invalid_argument(error.what());
    }

    if(
        value.diagnostics.verify_triangle
        && (value.renderer.width != 800 || value.renderer.height != 600)
    ){
        throw std::invalid_argument("diagnostics.verify_triangle requires an 800x600 image");
    }
}

} // namespace 

// Device Settings
void from_json(const Json& document, DeviceSettings& value)
{
    auto candidate = value;
    io::read_json_fields(document, candidate, device_fields);
    validate_device(candidate);
    value = candidate;
}

void to_json(Json& document, const DeviceSettings& value)
{
    validate_device(value);
    document = io::write_json_fields(value, device_fields);
}

// Renderer Settings
void from_json(const Json& document, RendererSettings& value)
{
    auto candidate = value;
    io::read_json_fields(document, candidate, renderer_fields);
    validate_renderer(candidate);
    value = candidate;
}

void to_json(Json& document, const RendererSettings& value)
{
    validate_renderer(value);
    document = io::write_json_fields(value, renderer_fields);
}

// Diagnostics Settings
void from_json(const Json& document, DiagnosticsSettings& value)
{
    io::read_json_fields(document, value, diagnostics_fields);
}

void to_json(Json& document, const DiagnosticsSettings& value)
{
    document = io::write_json_fields(value, diagnostics_fields);
}

// Render Settings
void from_json(const Json& document, RenderConfig& value)
{
    RenderConfig candidate{};

    io::read_json_fields(document, candidate, config_fields);
    validate_config(candidate);

    value = std::move(candidate);
}

void to_json(Json& document, const RenderConfig& value)
{
    validate_config(value);
    document = io::write_json_fields(value, config_fields);
}


} // namespace renderer::config