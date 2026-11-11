#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <istream>

namespace renderer::config_io
{

[[nodiscard]] nlohmann::json parse_json_document(std::istream& input);
[[nodiscard]] nlohmann::json load_json_document(const std::filesystem::path& path);

void save_json_document(
    const std::filesystem::path& filepath,  // 保存したい場所
    const nlohmann::json& document          // 保存したいデータ
);

} // namespace renderer::config_io