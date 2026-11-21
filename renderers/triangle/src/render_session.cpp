#include <renderer/render_session.hpp>
#include <renderer/optix_backend/device_context.hpp>
#include <renderer/optix_backend/error.hpp>
#include <renderer/image_types/image_layout.hpp>
#include "pipeline.hpp"
#include "launch_params.hpp"
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <cuda_driver_wrapper/pinned_host_buffer.hpp>
#include <cuda_driver_wrapper/pinned_host_memory.hpp>
#include <cuda_driver_wrapper/read_binary_file.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <optix_stubs.h>
#include <vector_types.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <limits>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace renderer::triangle
{
namespace cdw = cuda_driver_wrapper;
namespace
{
struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord
{ char header[OPTIX_SBT_RECORD_HEADER_SIZE]; };
static_assert(sizeof(SbtRecord) % OPTIX_SBT_RECORD_ALIGNMENT == 0);
static_assert(std::is_trivially_copyable_v<LaunchParams>);
static_assert(std::is_standard_layout_v<LaunchParams>);
static_assert(sizeof(float3) == 3 * sizeof(float));

SessionOptions checked_options(SessionOptions options)
{
    static_cast<void>(packed_image_bytes(options.image.width, options.image.height, sizeof(RGB32f)));
    if (options.frame_slots == 0)
        throw std::invalid_argument("Frame slot count must be positive");
    return options;
}
std::shared_ptr<cdw::RetainPrimaryContext> retain(CUdevice device)
{
    cdw::initialize_driver();
    return std::make_shared<cdw::RetainPrimaryContext>(device);
}
// A lease keeps ONLY pixels, their completion event and the CUDA context alive.
// No pipeline, GAS, scratch, upload buffer, OptiX context or session is retained.
struct FrameStorage
{
    std::shared_ptr<cdw::RetainPrimaryContext> context_;
    cdw::DeviceBuffer pixels_;
    cdw::Event completed_;
    bool recorded_ = false; // accessed only by session's CPU owner
    FrameStorage(std::shared_ptr<cdw::RetainPrimaryContext> context, std::size_t bytes)
        : context_(std::move(context)), pixels_(context_->handle(), bytes),
          completed_(context_->handle(), static_cast<unsigned int>(CU_EVENT_DISABLE_TIMING) | static_cast<unsigned int>(CU_EVENT_BLOCKING_SYNC)) {}
};
struct Slot
{
    std::shared_ptr<FrameStorage> frame;
    cdw::DeviceBuffer parameters;
    cdw::PinnedHostBuffer upload;
    Slot(const std::shared_ptr<cdw::RetainPrimaryContext>& context, std::size_t bytes)
        : frame(std::make_shared<FrameStorage>(context, bytes)),
          parameters(context->handle(), sizeof(LaunchParams)),
          upload(context->handle(), sizeof(LaunchParams)) {}
};
}

struct RenderSession::Impl
{
    SessionOptions options_;
    std::shared_ptr<cdw::RetainPrimaryContext> cuda_;
    optix_backend::DeviceContext optix_;
    cdw::Stream stream_;
    std::unique_ptr<detail::Pipeline> pipeline_;
    cdw::DeviceBuffer vertices_;
    cdw::DeviceBuffer sbt_buffer_;
    cdw::DeviceBuffer gas_;
    cdw::DeviceBuffer scratch_;
    cdw::PinnedHostBuffer vertex_upload_;
    cdw::PinnedHostBuffer sbt_upload_;
    cdw::Event initialized_;
    std::vector<Slot> slots_;
    OptixShaderBindingTable sbt_{};
    SessionStatistics statistics_{};
    const cdw::Event* last_completion_ = nullptr;
    bool pending_ = false;
    bool poisoned_ = false;
    bool setup_retired_ = false;

    Impl(CUdevice device, const std::filesystem::path& module,
        SessionOptions options, const Report& report)
        : options_(checked_options(options)), cuda_(retain(device)),
          optix_(cuda_->handle(), options_.optix_validation), stream_(cuda_->handle()),
          initialized_(cuda_->handle(), static_cast<unsigned int>(CU_EVENT_DISABLE_TIMING) | static_cast<unsigned int>(CU_EVENT_BLOCKING_SYNC))
    {
        try
        {
            if (report) report("Creating the persistent OptiX pipeline...");
            pipeline_ = std::make_unique<detail::Pipeline>(optix_,
                std::span<const char>(cdw::read_binary_file(module)));
            ++statistics_.pipeline_builds;
            const auto context = cuda_->handle();
            vertices_ = cdw::DeviceBuffer(context, 3 * sizeof(float3));
            sbt_buffer_ = cdw::DeviceBuffer(context, 3 * sizeof(SbtRecord));
            vertex_upload_ = cdw::PinnedHostBuffer(context, vertices_.size_bytes());
            sbt_upload_ = cdw::PinnedHostBuffer(context, sbt_buffer_.size_bytes());
            const std::array<float3, 3> vertices{{
                {-0.75f, -0.65f, 0.0f}, {0.75f, -0.65f, 0.0f}, {0.0f, 0.75f, 0.0f}}};
            std::memcpy(vertex_upload_.bytes().data(), vertices.data(), sizeof(vertices));
            CUdeviceptr vertex_address = vertices_.device_ptr();
            unsigned int flags = static_cast<unsigned int>(OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT);
            OptixBuildInput input{};
            input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
            input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
            input.triangleArray.vertexStrideInBytes = sizeof(float3);
            input.triangleArray.numVertices = 3;
            input.triangleArray.vertexBuffers = &vertex_address;
            input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_NONE;
            input.triangleArray.flags = &flags;
            input.triangleArray.numSbtRecords = 1;
            OptixAccelBuildOptions build{};
            build.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
            build.operation = OPTIX_BUILD_OPERATION_BUILD;
            cdw::ScopedCurrentContext current(context);
            OptixAccelBufferSizes sizes{};
            optix_backend::check_optix(optixAccelComputeMemoryUsage(
                optix_.handle(), &build, &input, 1, &sizes), "optixAccelComputeMemoryUsage");
            gas_ = cdw::DeviceBuffer(context, sizes.outputSizeInBytes);
            scratch_ = cdw::DeviceBuffer(context, sizes.tempSizeInBytes);
            std::array<SbtRecord, 3> records{};
            optix_backend::check_optix(optixSbtRecordPackHeader(pipeline_->raygen_program(), &records[0]),
                "optixSbtRecordPackHeader(raygen)");
            optix_backend::check_optix(optixSbtRecordPackHeader(pipeline_->miss_program(), &records[1]),
                "optixSbtRecordPackHeader(miss)");
            optix_backend::check_optix(optixSbtRecordPackHeader(pipeline_->hitgroup_program(), &records[2]),
                "optixSbtRecordPackHeader(hitgroup)");
            std::memcpy(sbt_upload_.bytes().data(), records.data(), sizeof(records));
            sbt_.raygenRecord = sbt_buffer_.device_ptr();
            sbt_.missRecordBase = sbt_buffer_.device_ptr() + sizeof(SbtRecord);
            sbt_.missRecordStrideInBytes = sizeof(SbtRecord);
            sbt_.missRecordCount = 1;
            sbt_.hitgroupRecordBase = sbt_buffer_.device_ptr() + 2 * sizeof(SbtRecord);
            sbt_.hitgroupRecordStrideInBytes = sizeof(SbtRecord);
            sbt_.hitgroupRecordCount = 1;
            const auto bytes = packed_image_bytes(options_.image.width, options_.image.height, sizeof(RGB32f));
            slots_.reserve(options_.frame_slots);
            for (std::uint32_t i = 0; i < options_.frame_slots; ++i)
            {
                slots_.emplace_back(cuda_, bytes);
                ++statistics_.frame_allocations;
            }
            if (report) report("Uploading the scene and building its GAS once...");
            pending_ = true; // before ANY call which can enqueue then throw
            cdw::copy_host_to_device_async(vertices_.view(), vertex_upload_.view(), stream_);
            ++statistics_.geometry_uploads;
            cdw::copy_host_to_device_async(sbt_buffer_.view(), sbt_upload_.view(), stream_);
            ++statistics_.sbt_uploads;
            OptixTraversableHandle traversable = 0;
            optix_backend::check_optix(optixAccelBuild(optix_.handle(), stream_.handle(),
                &build, &input, 1, scratch_.device_ptr(), scratch_.size_bytes(),
                gas_.device_ptr(), gas_.size_bytes(), &traversable, nullptr, 0), "optixAccelBuild");
            ++statistics_.gas_builds;
            // This fixed scene has immutable launch parameters. Upload each
            // slot's pointer/extent once, not on every render or UI frame.
            for (auto& slot : slots_)
            {
                const LaunchParams params{reinterpret_cast<RGB32f*>(slot.frame->pixels_.device_ptr()),
                    options_.image.width, options_.image.height, traversable};
                std::memcpy(slot.upload.bytes().data(), &params, sizeof(params));
                cdw::copy_host_to_device_async(slot.parameters.view(), slot.upload.view(), stream_);
                ++statistics_.parameter_uploads;
            }
            cdw::record_event(initialized_, stream_);
            last_completion_ = &initialized_;
            current.restore();
            if (report) report("Scene initialization submitted; renderer session is reusable.");
        }
        catch (...)
        {
            // Constructor failure has no Impl destructor. Drain BEFORE members
            // and upload memory are destroyed, even if restore()/record failed.
            poisoned_ = true;
            drain_noexcept();
            throw;
        }
    }
    ~Impl() noexcept { drain_noexcept(); }
    void wait_idle()
    {
        if (!pending_) return;
        if (poisoned_ || !last_completion_)
            cdw::synchronize_stream(stream_); // exceptional partial submission
        else
            cdw::synchronize_event(*last_completion_); // BLOCKING_SYNC event
        pending_ = false;
    }
    void drain_noexcept() noexcept
    {
        try { wait_idle(); }
        catch (const std::exception& error)
        { std::fprintf(stderr, "[triangle session cleanup] %s\n", error.what()); }
        catch (...) { std::fputs("[triangle session cleanup] unknown failure\n", stderr); }
    }
    bool collect()
    {
        if (setup_retired_) return false;
        if (!cdw::is_event_ready(initialized_)) return true;
        // Called by the execution owner. No frame lease retains these resources.
        scratch_.close();
        vertex_upload_.close();
        sbt_upload_.close();
        for (auto& slot : slots_) slot.upload.close();
        setup_retired_ = true;
        return false;
    }
    std::optional<gpu::SubmittedRGB32fImage> try_render()
    {
        if (poisoned_) throw std::logic_error("RenderSession failed; reconstruct it before use");
        if (statistics_.render_launches == (std::numeric_limits<std::uint64_t>::max)())
            throw std::overflow_error("Render sequence exhausted");
        static_cast<void>(collect());
        for (auto& slot : slots_)
        {
            if (slot.frame.use_count() != 1) continue; // held by ANY consumer
            if (slot.frame->recorded_ && !cdw::is_event_ready(slot.frame->completed_)) continue;
            try
            {
                pending_ = true;
                cdw::ScopedCurrentContext current(cuda_->handle());
                if (options_.diagnose_unwritten_pixels)
                {
                    cdw::fill_device_bytes_async(slot.frame->pixels_.view(), 0xff, stream_);
                    ++statistics_.diagnostic_clears;
                }
                // Setup and all previous renders are ordered on the same stream.
                // No host sync, per-request GAS build, allocation or parameter copy.
                optix_backend::check_optix(optixLaunch(pipeline_->handle(), stream_.handle(),
                    slot.parameters.device_ptr(), slot.parameters.size_bytes(), &sbt_,
                    options_.image.width, options_.image.height, 1), "optixLaunch");
                cdw::record_event(slot.frame->completed_, stream_);
                slot.frame->recorded_ = true;
                last_completion_ = &slot.frame->completed_;
                current.restore();
                ++statistics_.render_launches;
                const gpu::ConstRGB32fImageView image{std::as_const(slot.frame->pixels_).view(),
                    options_.image.width, options_.image.height,
                    checked_row_bytes(options_.image.width, sizeof(RGB32f))};
                return gpu::SubmittedRGB32fImage(image,
                    slot.frame,
                    std::shared_ptr<const cdw::Event>(slot.frame, &slot.frame->completed_),
                    cuda_, statistics_.render_launches);
            }
            catch (...) { poisoned_ = true; throw; }
        }
        return std::nullopt;
    }
};

RenderSession::RenderSession(CUdevice device, const std::filesystem::path& module,
    const SessionOptions& options, Report report)
    : impl_(std::make_unique<Impl>(device, module, options, report)) {}
RenderSession::~RenderSession() noexcept = default;
std::optional<gpu::SubmittedRGB32fImage> RenderSession::try_render() { return impl_->try_render(); }
bool RenderSession::collect() { return impl_->collect(); }
void RenderSession::wait_idle() { impl_->wait_idle(); static_cast<void>(impl_->collect()); }
CUcontext RenderSession::context() const noexcept { return impl_->cuda_->handle(); }
std::shared_ptr<const void> RenderSession::context_lifetime() const { return impl_->cuda_; }
Settings RenderSession::settings() const noexcept { return impl_->options_.image; }
SessionStatistics RenderSession::statistics() const noexcept { return impl_->statistics_; }
}
