#pragma once

#include <renderer/optix_backend/device_context.hpp>

#include <span>

namespace renderer::triangle::detail
{

// The triangle's program names and compile options belong to this renderer.
// The OptiX/CUDA contexts are borrowed; pending work must finish before destruction.
class Pipeline final
{
public:
    Pipeline(const optix_backend::DeviceContext& context, std::span<const char> module_code);
    ~Pipeline() noexcept;

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    Pipeline(Pipeline&&) = delete;
    Pipeline& operator=(Pipeline&&) = delete;

    [[nodiscard]] OptixPipeline handle() const noexcept { return pipeline_; }
    [[nodiscard]] OptixProgramGroup raygen_program() const noexcept { return raygen_; }
    [[nodiscard]] OptixProgramGroup miss_program() const noexcept { return miss_; }
    [[nodiscard]] OptixProgramGroup hitgroup_program() const noexcept { return hitgroup_; }

private:
    void close_noexcept() noexcept;

    CUcontext cuda_context_ = nullptr;
    OptixModule module_ = nullptr;
    OptixProgramGroup raygen_ = nullptr;
    OptixProgramGroup miss_ = nullptr;
    OptixProgramGroup hitgroup_ = nullptr;
    OptixPipeline pipeline_ = nullptr;
};

} // namespace renderer::triangle::detail
