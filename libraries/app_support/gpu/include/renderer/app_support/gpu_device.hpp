#pragma once

#include <renderer/app_support/device_settings.hpp>

#include <cuda.h>

#include <filesystem>
#include <iosfwd>

namespace renderer::app_support
{

// CLI の起動時にドライバを初期化し，選択したデバイスと実行環境を log に記録する
[[nodiscard]] CUdevice initialize_cli_device(
    const DeviceSettings& settings,
    const std::filesystem::path& module_directory,
    std::ostream& log
);

} // namespace renderer::app_support