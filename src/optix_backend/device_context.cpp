#include <renderer/optix_backend/device_context.hpp>
#include <renderer/optix_backend/error.hpp>

#include <cuda_driver_wrapper/context.hpp>

// Exactly one translation unit defines the process-wide OptiX function table.
#include <optix_function_table_definition.h>
#include <optix_stubs.h>

#include <cstdio>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace renderer::optix_backend
{
namespace
{

void log_callback(unsigned int level, const char* tag, const char* message, void*) noexcept
{
    std::fprintf(stderr, "[OptiX %u][%s] %s\n", level,
        tag != nullptr ? tag : "", message != nullptr ? message : "");
}

void initialize_optix()
{
    static std::once_flag once;
    std::call_once(once, [] { check_optix(optixInit(), "optixInit"); });
}

} // namespace

DeviceContext::DeviceContext(CUcontext cuda_context, bool enable_validation)
    : cuda_context_(cuda_context)
{
    if (cuda_context_ == nullptr)
    {
        throw std::invalid_argument("DeviceContext requires a non-null CUDA context");
    }
    cuda_driver_wrapper::ScopedCurrentContext current(cuda_context_);
    try
    {
        initialize_optix();
        OptixDeviceContextOptions options{};
        options.logCallbackFunction = &log_callback;
        options.logCallbackLevel = 4;
        options.validationMode = enable_validation
            ? OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_ALL
            : OPTIX_DEVICE_CONTEXT_VALIDATION_MODE_OFF;
        check_optix(optixDeviceContextCreate(cuda_context_, &options, &context_),
            "optixDeviceContextCreate");
        current.restore();
    }
    catch (...)
    {
        // Also handles a restore() failure after successful creation.
        close_noexcept();
        throw;
    }
}

DeviceContext::~DeviceContext() noexcept
{
    close_noexcept();
}

void DeviceContext::close_noexcept() noexcept
{
    if (context_ == nullptr)
    {
        return;
    }
    try
    {
        cuda_driver_wrapper::ScopedCurrentContext current(cuda_context_);
        report_optix_cleanup(optixDeviceContextDestroy(std::exchange(context_, nullptr)),
            "optixDeviceContextDestroy");
        current.restore();
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "[OptiX context cleanup] %s\n", error.what());
    }
    catch (...)
    {
        std::fputs("[OptiX context cleanup] unknown failure\n", stderr);
    }
}

} // namespace renderer::optix_backend
