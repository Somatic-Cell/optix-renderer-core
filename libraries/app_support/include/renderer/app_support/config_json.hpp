#pragma once 

#include <renderer/app_support/device_settings.hpp>
#include <renderer/config_io/json_document.hpp>
#include <renderer/image_export/export_settings.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace renderer::app_support
{
void from_json(
    const nlohmann::json& document,
    DeviceSettings& value
);

void to_json(
    nlohmann::json& document,
    const DeviceSettings& value
);

struct OutputSettingsCodec
{
    void read(const nlohmann::json& document, image_export::ExportSettings& value) const;
    [[nodiscard]] nlohmann::json write(const image_export::ExportSettings& value) const;
};

// 読み込み時にバージョンをたしかめる
// 書き込み時には対応しているバージョンを出力する
template<class Fields>
void bind_schema_version(Fields& fields)
{
    // TODO: バージョンが直書き
    constexpr std::uint32_t supported_version = 1;
    auto version = supported_version;
    fields.required("schema_version", version);
    if(version != supported_version)
    {
        throw std::invalid_argument("schema version: unsupported schema version; expected" + std::to_string(supported_version));
    }
}

namespace detail
{
[[nodiscard]] std::filesystem::path absolute_config_path(const std::filesystem::path& path);

[[nodiscard]] std::filesystem::path output_path_for_save(
    const std::filesystem::path& config_directory,
    const std::filesystem::path& output
);
    
} // namespace detail

// Config は Json に変換可能
template<class Config>
[[nodiscard]] Config load_config_file(const std::filesystem::path& path)
{
    const auto config_path = detail::absolute_config_path(path);
    const auto document = config_io::load_json_document(config_path);

    try
    {
        auto config = document.get<Config>();
        config.output.path = (config_path.parent_path() / config.output.path).lexically_normal();
        return config;
    }
    catch(const std::invalid_argument& error)
    {
        const auto name = config_path.generic_u8string();
        throw std::invalid_argument(
            "Configuration '" + std::string(name.begin(), name.end()) + "': " + error.what()
        );
    }
    
}

template<class Config>
void save_config_file(
    const std::filesystem::path& path,
    const Config& config
)
{
    const auto config_path = detail::absolute_config_path(path);
    auto stored = config;

    stored.output.path = detail::output_path_for_save(config_path.parent_path(), config.output.path);
    config_io::save_json_document(config_path, nlohmann::json(stored));
}


} // namespace renderer::app_support