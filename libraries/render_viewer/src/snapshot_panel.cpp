#include "snapshot_panel.hpp"

#include <renderer/render_viewer/viewer.hpp>
#include <imgui.h>

#include <chrono>
#include <ctime>
#include <stdexcept>
#include <string>
#include <string_view>

namespace renderer::render_viewer
{
namespace
{
std::filesystem::path utf8_path(std::string_view text)
{
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

std::filesystem::path automatic_name()
{
    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
#if defined(_WIN32)
    if (::localtime_s(&local, &now) != 0)
#else
    if (!::localtime_r(&now, &local))
#endif
        throw std::runtime_error("Cannot obtain local time for snapshot name");
    char name[64];
    if (std::strftime(name, sizeof(name), "snapshot_%Y-%m-%d_%H%M%S.png", &local) == 0)
        throw std::runtime_error("Cannot format snapshot name");
    return name;
}
} // namespace

std::optional<std::filesystem::path> SnapshotPanel::draw(bool can_save, const SnapshotStatus& status)
{
    ImGui::Separator();
    ImGui::TextUnformatted("Save snapshot (PNG)");
    ImGui::InputText("Folder", folder_, sizeof(folder_));
    ImGui::InputTextWithHint("Filename", "Automatic if empty", filename_, sizeof(filename_));
    ImGui::BeginDisabled(!can_save);
    const bool clicked = ImGui::Button("Save Snapshot");
    ImGui::EndDisabled();
    if (!status.message.empty()) ImGui::TextWrapped("%s", status.message.c_str());
    if (!clicked) return std::nullopt;

    if (folder_[0] == '\0') throw std::invalid_argument("Enter an output folder");
    auto name = filename_[0] == '\0' ? automatic_name() : utf8_path(filename_);
    if (name.has_parent_path() || name.has_root_path() || name == "." || name == "..")
        throw std::invalid_argument("Use the Folder field for the directory");
    if (name.extension().empty()) name += ".png";
    return utf8_path(folder_) / name;
}
} // namespace renderer::render_viewer
