#include <renderer/renderers/triangle.hpp>
#include <renderer/optix_backend/device_context.hpp>
#include <renderer/optix_backend/error.hpp>

#include "launch_params.hpp"
#include "pipeline.hpp"

#include <cuda_driver_wrapper/context.hpp>
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
#include <exception>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace renderer::triangle
{
namespace
{

namespace cuda = cuda_driver_wrapper;

struct alignas(OPTIX_SBT_RECORD_ALIGNMENT) SbtRecord
{
    char header[OPTIX_SBT_RECORD_HEADER_SIZE];
};
static_assert(sizeof(SbtRecord) % OPTIX_SBT_RECORD_ALIGNMENT == 0);
static_assert(std::is_trivially_copyable_v<LaunchParams>);
static_assert(std::is_standard_layout_v<LaunchParams>);
static_assert(sizeof(float3) == 3 * sizeof(float));

std::size_t checked_pixel_count(Settings settings)
{
    if (settings.width == 0 || settings.height == 0)
    {
        throw std::invalid_argument("Image dimensions must be positive");
    }
    return cuda::checked_byte_size(settings.width, settings.height);
}

} // namespace

struct RenderJob::Impl
{
    enum class State { idle, submitted, complete, consumed, failed };

    const optix_backend::DeviceContext& context;
    const cuda::Stream& stream;
    Settings settings;
    std::size_t pixel_count;
    std::size_t image_bytes;
    detail::Pipeline pipeline;

    cuda::DeviceBuffer device_vertices;
    cuda::DeviceBuffer device_sbt;
    cuda::DeviceBuffer device_params;
    cuda::DeviceBuffer device_pixels;
    cuda::DeviceBuffer scratch;
    cuda::DeviceBuffer gas;
    cuda::PinnedHostBuffer host_vertices;
    cuda::PinnedHostBuffer host_sbt;
    cuda::PinnedHostBuffer host_params;
    cuda::PinnedHostBuffer host_pixels;
    cuda::Event completed;

    CUdeviceptr vertex_address = 0;
    unsigned int geometry_flags = OPTIX_GEOMETRY_FLAG_DISABLE_ANYHIT;
    OptixBuildInput build_input{};
    OptixAccelBuildOptions build_options{};
    OptixShaderBindingTable sbt{};
    State state = State::idle;
    bool pending = false;

    Impl(const optix_backend::DeviceContext& context_in,
         const cuda::Stream& stream_in,
         const std::filesystem::path& module_path,
         Settings settings_in)
        : context(context_in), stream(stream_in), settings(settings_in),
          pixel_count(checked_pixel_count(settings)),
          image_bytes(cuda::checked_byte_size(pixel_count, sizeof(RGB32f))),
          pipeline(context, std::span<const char>(cuda::read_binary_file(module_path))),
          device_vertices(context.cuda_context(), 3 * sizeof(float3)),
          device_sbt(context.cuda_context(), 3 * sizeof(SbtRecord)),
          device_params(context.cuda_context(), sizeof(LaunchParams)),
          device_pixels(context.cuda_context(), image_bytes),
          host_vertices(context.cuda_context(), 3 * sizeof(float3)),
          host_sbt(context.cuda_context(), 3 * sizeof(SbtRecord)),
          host_params(context.cuda_context(), sizeof(LaunchParams)),
          host_pixels(context.cuda_context(), image_bytes),
          completed(context.cuda_context())
    {
        if (!stream.is_valid() || stream.context() != context.cuda_context())
        {
            throw std::invalid_argument("The stream must belong to the supplied CUDA context");
        }
        // Construction prepares resources but enqueues no uploads/builds/launches.
        cuda::ScopedCurrentContext current(context.cuda_context());
        const std::array<float3, 3> vertices{{
            {-0.75f, -0.65f, 0.0f},
            { 0.75f, -0.65f, 0.0f},
            { 0.00f,  0.75f, 0.0f}
        }};
        std::memcpy(host_vertices.bytes().data(), vertices.data(), sizeof(vertices));

        vertex_address = device_vertices.device_ptr();
        build_input.type = OPTIX_BUILD_INPUT_TYPE_TRIANGLES;
        build_input.triangleArray.vertexFormat = OPTIX_VERTEX_FORMAT_FLOAT3;
        build_input.triangleArray.vertexStrideInBytes = sizeof(float3);
        build_input.triangleArray.numVertices = static_cast<unsigned int>(vertices.size());
        build_input.triangleArray.vertexBuffers = &vertex_address;
        build_input.triangleArray.indexFormat = OPTIX_INDICES_FORMAT_NONE;
        build_input.triangleArray.flags = &geometry_flags;
        build_input.triangleArray.numSbtRecords = 1;
        build_options.buildFlags = OPTIX_BUILD_FLAG_PREFER_FAST_TRACE;
        build_options.operation = OPTIX_BUILD_OPERATION_BUILD;

        OptixAccelBufferSizes sizes{};
        optix_backend::check_optix(optixAccelComputeMemoryUsage(
            context.handle(), &build_options, &build_input, 1, &sizes),
            "optixAccelComputeMemoryUsage");
        scratch = cuda::DeviceBuffer(context.cuda_context(), sizes.tempSizeInBytes);
        gas = cuda::DeviceBuffer(context.cuda_context(), sizes.outputSizeInBytes);

        std::array<SbtRecord, 3> records{};
        optix_backend::check_optix(optixSbtRecordPackHeader(pipeline.raygen_program(), &records[0]),
            "optixSbtRecordPackHeader(raygen)");
        optix_backend::check_optix(optixSbtRecordPackHeader(pipeline.miss_program(), &records[1]),
            "optixSbtRecordPackHeader(miss)");
        optix_backend::check_optix(optixSbtRecordPackHeader(pipeline.hitgroup_program(), &records[2]),
            "optixSbtRecordPackHeader(hitgroup)");
        std::memcpy(host_sbt.bytes().data(), records.data(), sizeof(records));

        sbt.raygenRecord = device_sbt.device_ptr();
        sbt.missRecordBase = device_sbt.device_ptr() + sizeof(SbtRecord);
        sbt.missRecordStrideInBytes = sizeof(SbtRecord);
        sbt.missRecordCount = 1;
        sbt.hitgroupRecordBase = device_sbt.device_ptr() + 2 * sizeof(SbtRecord);
        sbt.hitgroupRecordStrideInBytes = sizeof(SbtRecord);
        sbt.hitgroupRecordCount = 1;
        current.restore();
    }

    ~Impl() noexcept
    {
        if (!pending)
        {
            return;
        }
        // Runs BEFORE member destruction. An unsuccessful submit may already
        // have queued work, so its possibly-unrecorded Event is insufficient.
        try
        {
            cuda::synchronize_stream(stream);
        }
        catch (const std::exception& error)
        {
            std::fprintf(stderr, "[triangle job cleanup] Stream completion failed: %s\n", error.what());
        }
        catch (...)
        {
            std::fputs("[triangle job cleanup] Stream completion failed\n", stderr);
        }
    }

    void submit()
    {
        if (state != State::idle)
        {
            throw std::logic_error("A triangle RenderJob can be submitted only once");
        }
        // Register the entire lifetime BEFORE the first asynchronous operation.
        // Until all calls including restore() succeed, no result is publishable.
        pending = true;
        state = State::failed;
        cuda::ScopedCurrentContext current(context.cuda_context());
        cuda::copy_host_to_device_async(device_vertices.view(), host_vertices.view(), stream);
        cuda::copy_host_to_device_async(device_sbt.view(), host_sbt.view(), stream);
        // NaN sentinel exposes pixels that the raygen program did not write.
        cuda::fill_device_bytes_async(device_pixels.view(), 0xff, stream);

        OptixTraversableHandle traversable = 0;
        optix_backend::check_optix(optixAccelBuild(
            context.handle(), stream.handle(), &build_options, &build_input, 1,
            scratch.device_ptr(), scratch.size_bytes(), gas.device_ptr(), gas.size_bytes(),
            &traversable, nullptr, 0), "optixAccelBuild");

        const LaunchParams params{
            reinterpret_cast<RGB32f*>(device_pixels.device_ptr()),
            settings.width, settings.height, traversable
        };
        std::memcpy(host_params.bytes().data(), &params, sizeof(params));
        cuda::copy_host_to_device_async(device_params.view(), host_params.view(), stream);
        optix_backend::check_optix(optixLaunch(
            pipeline.handle(), stream.handle(), device_params.device_ptr(),
            device_params.size_bytes(), &sbt, settings.width, settings.height, 1),
            "optixLaunch");
        cuda::copy_device_to_host_async(host_pixels.view(), device_pixels.view(), stream);
        cuda::record_event(completed, stream);
        current.restore();
        state = State::submitted;
    }

    bool is_ready()
    {
        switch (state)
        {
        case State::idle:
            return false;
        case State::failed:
            throw std::logic_error("The triangle job failed; no image can be published");
        case State::complete:
        case State::consumed:
            return true;
        case State::submitted:
            if (!cuda::is_event_ready(completed))
            {
                return false;
            }
            state = State::complete;
            pending = false;
            return true;
        }
        throw std::logic_error("Invalid triangle job state");
    }

    void wait()
    {
        if (state == State::idle || state == State::failed)
        {
            throw std::logic_error("wait() requires a successfully submitted triangle job");
        }
        if (state == State::submitted)
        {
            cuda::synchronize_event(completed);
            state = State::complete;
            pending = false;
        }
    }

    LinearRGBImage take_image()
    {
        if (state == State::consumed)
        {
            throw std::logic_error("The triangle image has already been taken");
        }
        if (!is_ready())
        {
            throw std::logic_error("The triangle image is not ready; wait or poll first");
        }
        LinearRGBImage image{settings.width, settings.height, std::vector<RGB32f>(pixel_count)};
        std::memcpy(image.pixels.data(), host_pixels.bytes().data(), image_bytes);
        state = State::consumed;
        return image;
    }
};

RenderJob::RenderJob(
    const optix_backend::DeviceContext& context,
    const cuda_driver_wrapper::Stream& stream,
    const std::filesystem::path& module_path,
    Settings settings)
    : impl_(std::make_unique<Impl>(context, stream, module_path, settings))
{
}

RenderJob::~RenderJob() noexcept = default;
void RenderJob::submit() { impl_->submit(); }
bool RenderJob::is_ready() { return impl_->is_ready(); }
void RenderJob::wait() { impl_->wait(); }
LinearRGBImage RenderJob::take_image() { return impl_->take_image(); }

} // namespace renderer::triangle
