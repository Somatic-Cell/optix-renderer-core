#pragma once

#include <filesystem>
#include <optional>

namespace renderer::render_viewer
{


// UI-thread state: edits remain local until the user actually presses Save.
class SnapshotPanel
{
public:
    void request_open() noexcept { open_requested_ = true; }
    [[nodiscard]] std::optional<std::filesystem::path> draw(bool can_save);

private:
    bool open_requested_ = false;
    char folder_[1024] = "output/snapshots";
    char filename_[256] = {};
};

} // namespace renderer::render_viewer
