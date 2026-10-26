#include <renderer/optix_backend/error.hpp>

#include <optix_stubs.h>

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace renderer::optix_backend
{
namespace
{

const char* error_name(OptixResult result) noexcept
{
    // SDK stubs provide fallback strings even when optixInit could not load
    // the driver library. Do not depend on the SDK's function-table symbol.
    const char* name = optixGetErrorName(result);
    return name != nullptr ? name : "unknown OptiX error";
}

const char* error_description(OptixResult result) noexcept
{
    const char* description = optixGetErrorString(result);
    return description != nullptr ? description : "no description";
}

} // namespace

void check_optix(
    OptixResult result, const char* operation, std::source_location location)
{
    if (result == OPTIX_SUCCESS)
    {
        return;
    }
    std::string message(operation != nullptr ? operation : "OptiX operation");
    message += " failed: ";
    message += error_name(result);
    message += " (" + std::to_string(static_cast<int>(result)) + "): ";
    message += error_description(result);
    message += "\n";
    message += location.file_name();
    message += ":" + std::to_string(location.line());
    throw std::runtime_error(message);
}

void check_optix_log(
    OptixResult result,
    const char* operation,
    std::span<const char> log,
    std::size_t reported_log_size,
    std::source_location location)
{
    if (result == OPTIX_SUCCESS)
    {
        return;
    }
    std::string message(operation != nullptr ? operation : "OptiX compilation");
    const auto end = std::find(log.begin(), log.end(), '\0');
    if (end != log.begin())
    {
        message += "\nOptiX compilation log:\n";
        message.append(log.begin(), end);
    }
    if (reported_log_size > log.size())
    {
        message += "\n[Compilation log truncated.]";
    }
    check_optix(result, message.c_str(), location);
}

void report_optix_cleanup(OptixResult result, const char* operation) noexcept
{
    if (result != OPTIX_SUCCESS)
    {
        std::fprintf(stderr, "[OptiX cleanup] %s: %s (%d): %s\n",
            operation, error_name(result), static_cast<int>(result),
            error_description(result));
    }
}

} // namespace renderer::optix_backend
