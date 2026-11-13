#pragma once

#include <renderer/gpu/image_view.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

#include <cuda_driver_wrapper/kernel.hpp>
#include <cuda_driver_wrapper/module.hpp>

#include <filesystem>

namespace renderer::image_processing
{

class OutputTransform final
{
public:
    OutputTransform(CUcontext context, const std::filesystem::path& module_path);
    ~OutputTransform() noexcept = default;

    // copy / move の禁止
    OutputTransform(const OutputTransform&) = delete;
    OutputTransform& operator=(const OutputTransform&) = delete;
    OutputTransform(OutputTransform&&) = delete;
    OutputTransform& operator=(const OutputTransform&&) = delete;

    [[nodiscard]] CUcontext context() const noexcept { return module_.context(); }

    void enqueue(
        gpu::ConstLinearRGBImageView input,
        gpu::SRGBImageView output,
        const cuda_driver_wrapper::Stream& stream,
        const OutputTransformSettings& settings = {}
    ) const;

private:
    cuda_driver_wrapper::Module module_;
    cuda_driver_wrapper::Kernel kernel_;
};

} // namespace renderer::image_processing