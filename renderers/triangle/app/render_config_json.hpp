#pragma once 

#include "render_config.hpp"
#include <nlohmann/json.hpp>

namespace renderer::config
{

void from_json(const nlohmann::json& document, DeviceSettings& value);
void to_json(nlohmann::json& document, const DeviceSettings& value);

void from_json(const nlohmann::json& document, RendererSettings& value);
void to_json(nlohmann::json& document, const RendererSettings& value);

void from_json(const nlohmann::json& document, DiagnosticsSettings& value);
void to_json(nlohmann::json& document, const DiagnosticsSettings& value);

void from_json(const nlohmann::json& document, RenderConfig& value);
void to_json(nlohmann::json& document, const RenderConfig& value);

} // namespace renderer::config