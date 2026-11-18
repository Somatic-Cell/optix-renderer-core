#pragma once
#include <renderer/gpu/submitted_image.hpp>
#include <cuda.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace renderer::triangle
{
struct Settings { std::uint32_t width = 800; std::uint32_t height = 600; };
struct SessionOptions
{
    Settings image{};
    std::uint32_t frame_slots = 2;
    bool optix_validation = false;
    bool diagnose_unwritten_pixels = false;
};
struct SessionStatistics
{
    std::uint64_t pipeline_builds = 0;
    std::uint64_t gas_builds = 0;
    std::uint64_t geometry_uploads = 0;
    std::uint64_t sbt_uploads = 0;
    std::uint64_t parameter_uploads = 0;
    std::uint64_t frame_allocations = 0;
    std::uint64_t render_launches = 0;
    std::uint64_t diagnostic_clears = 0;
};

// A persistent fixed-scene renderer, NOT a GUI task. One CPU owner at a time.
// Construction prepares one pipeline/scene and enqueues initialization once.
// Subsequent requests launch into free slots; no BVH rebuild, allocation, upload,
// full-frame clear (unless diagnostics requested), readback, or host wait.
// Returned leases can cross CPU threads. Release only AFTER GPU consumers end.
class RenderSession final
{
public:
    using Report = std::function<void(std::string)>;
    RenderSession(CUdevice device, const std::filesystem::path& module,
        const SessionOptions& options = {}, Report report = {});
    ~RenderSession() noexcept;
    RenderSession(const RenderSession&) = delete;
    RenderSession& operator=(const RenderSession&) = delete;
    RenderSession(RenderSession&&) = delete;
    RenderSession& operator=(RenderSession&&) = delete;

    // nullopt = all slots are leased or still executing. No sample/request is
    // silently discarded; the caller retains and retries its pending request.
    [[nodiscard]] std::optional<gpu::SubmittedRGB32fImage> try_render();
    // Nonblocking setup-retirement poll; true = call again later. The execution
    // owner releases initialization-only pinned memory/scratch, never the UI.
    bool collect();
    void wait_idle(); // explicit headless/shutdown wait, not a frame-loop step
    [[nodiscard]] CUcontext context() const noexcept;
    [[nodiscard]] std::shared_ptr<const void> context_lifetime() const;
    [[nodiscard]] Settings settings() const noexcept;
    [[nodiscard]] SessionStatistics statistics() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
