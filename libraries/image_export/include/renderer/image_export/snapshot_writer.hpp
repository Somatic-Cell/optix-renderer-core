#pragma once

#include <renderer/gpu/submitted_image.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

#include <filesystem>
#include <future>
#include <memory>
#include <optional>

namespace renderer::image_processing { class OutputTransform; }

namespace renderer::image_export
{

// One controller thread calls these methods. A save, once accepted, survives
// window closure. The destructor drains it; the UI loop uses poll(), never wait().
class SnapshotWriter final
{
public:
    SnapshotWriter() = default;
    ~SnapshotWriter() noexcept;

    SnapshotWriter(const SnapshotWriter&) = delete;
    SnapshotWriter& operator=(const SnapshotWriter&) = delete;
    SnapshotWriter(SnapshotWriter&&) = delete;
    SnapshotWriter& operator=(SnapshotWriter&&) = delete;

    // A completed operation remains busy until poll() consumes its result.
    [[nodiscard]] bool busy() const noexcept { return result_.valid(); }

    [[nodiscard]] bool try_submit(
        gpu::SubmittedRGB32fImage image,
        std::shared_ptr<const image_processing::OutputTransform> transform,
        image_processing::OutputTransformSettings settings,
        std::filesystem::path requested_path);

    // Empty = no completion. On success returns the actual, absolute path.
    // On failure throws once; the controller can then submit a new request.
    [[nodiscard]] std::optional<std::filesystem::path> poll();

private:
    std::future<std::filesystem::path> result_;
};

} // namespace renderer::image_export
