#pragma once

#include <optix.h>

#include <cstddef>
#include <source_location>
#include <span>
#include <stdexcept>
#include <string>

namespace renderer::optix_backend
{

class OptixError final : public std::runtime_error
{
public:
    OptixError(OptixResult result, const std::string& message);

    [[nodiscard]] OptixResult result() const noexcept;

private:
    OptixResult result_;
};

void check_optix(
    OptixResult result,
    const char* operation,
    std::source_location location = std::source_location::current()
);

void check_optix_log(
    OptixResult result,
    const char* operation,
    std::span<const char> log,
    std::size_t reported_log_size,
    std::source_location location = std::source_location::current()
);

void report_optix_cleanup(OptixResult, const char* operation) noexcept;

} // namespace renderer::optix_backend