#include <renderer/image_export/snapshot_writer.hpp>
#include <renderer/image_export/export_settings.hpp>
#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/image_io/png_writer.hpp>
#include <renderer/image_processing/output_transform.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <chrono>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <utility>

namespace renderer::image_export
{
namespace
{
namespace fs = std::filesystem;

// A result may be the last owner of the CUDA context. Retain it separately:
// the pixel lease can then be released as soon as the readback completes.
struct SaveRequest
{
    std::shared_ptr<const void> context_owner;
    std::shared_ptr<const image_processing::OutputTransform> transform;
    std::optional<gpu::SubmittedRGB32fImage> frame;
    ExportSettings output;
};

fs::path save_png(SaveRequest request)
{
    const auto input = request.frame->image();
    // The stream and the one-shot job are used only by this save worker.
    cuda_driver_wrapper::Stream stream(input.memory.context);
    RGB8ExportJob job(*request.transform, stream, input.width, input.height);
    job.submit(input, request.frame->completion_event(), request.output.transform);
    job.wait();
    request.frame.reset(); // Encoding and file I/O no longer need the renderer slot.
    fs::create_directories(request.output.path.parent_path());
    return image_io::write_new_png(request.output.path, job.view());
} // job dies before stream, and both before the owners, including on exceptions.

bool is_png(const fs::path& path)
{
    auto ext = path.extension().u8string();
    for (auto& c : ext)
        if (c >= u8'A' && c <= u8'Z') c = static_cast<char8_t>(c + (u8'a' - u8'A'));
    return ext == u8".png";
}
} // namespace

bool SnapshotWriter::try_submit(
    gpu::SubmittedRGB32fImage image,
    std::shared_ptr<const image_processing::OutputTransform> transform,
    image_processing::OutputTransformSettings settings,
    std::filesystem::path requested_path)
{
    if (busy()) return false;
    const auto input = image.image();
    if (!image.context_lifetime() || !transform || transform.use_count() == 0
        || transform->context() != input.memory.context)
        throw std::invalid_argument("Snapshot requires an owned image and transform in the same CUDA context");
    ExportSettings output{std::move(requested_path), ImageFileFormat::png, settings};
    validate_export_request(output, input.width, input.height);
    if (!is_png(output.path)) throw std::invalid_argument("Snapshot filename must end in .png");
    output.path = std::filesystem::absolute(output.path);
    result_ = std::async(std::launch::async, save_png, SaveRequest{
        image.context_lifetime(), std::move(transform), std::move(image), std::move(output)});
    return true;
}

std::optional<std::filesystem::path> SnapshotWriter::poll()
{
    if (!busy() || result_.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        return std::nullopt;
    return result_.get();
}

SnapshotWriter::~SnapshotWriter() noexcept
{
    if (!busy()) return;
    try { static_cast<void>(result_.get()); }
    catch (const std::exception& error) { std::fprintf(stderr, "Snapshot save: %s\n", error.what()); }
    catch (...) { std::fputs("Snapshot save: unknown failure\n", stderr); }
}

} // namespace renderer::image_export
