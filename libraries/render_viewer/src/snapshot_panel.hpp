#pragma once

#include <filesystem>
#include <optional>

namespace renderer::render_viewer
{

struct SnapshotStatus;

// UI-thread state: edits remain local until the user actually presses Save.
class SnapshotPanel
{
public:
    [[nodiscard]] std::optional<std::filesystem::path> draw(bool can_save, const SnapshotStatus& status);

private:
    char folder_[1024] = "output/snapshots";
    char filename_[256] = {};
};

} // namespace renderer::render_viewer
