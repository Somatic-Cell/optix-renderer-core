#pragma once

#include <renderer/gpu/image_view.hpp>
#include <renderer/gpu/surface_view.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

#include <cuda_driver_wrapper/kernel.hpp>
#include <cuda_driver_wrapper/module.hpp>

#include <filesystem>

namespace renderer::image_processing
{

// モジュールのルートディレクトリから，出力変換に使う fatbin のパスを組み立てる
[[nodiscard]] std::filesystem::path output_transform_module_path(
    const std::filesystem::path& module_directory
);

class OutputTransform final
{
public:
    OutputTransform(CUcontext context, const std::filesystem::path& module_path);
    ~OutputTransform() noexcept = default;

    // copy / move の禁止
    OutputTransform(const OutputTransform&) = delete;
    OutputTransform& operator=(const OutputTransform&) = delete;
    OutputTransform(OutputTransform&&) = delete;
    OutputTransform& operator=(OutputTransform&&) = delete;

    [[nodiscard]] CUcontext context() const noexcept { return module_.context(); }

    void enqueue(
        gpu::ConstRGB32fImageView input,
        gpu::RGB8ImageView output,
        const cuda_driver_wrapper::Stream& stream,
        const OutputTransformSettings& settings = {}
    ) const;
    
    void enqueue(
        gpu::ConstRGB32fImageView input,
        gpu::RGBA8SurfaceView output,
        const cuda_driver_wrapper::Stream& stream,
        const OutputTransformSettings& settings = {}
    ) const;

private:
    cuda_driver_wrapper::Module module_;
    cuda_driver_wrapper::Kernel linear_kernel_;
    cuda_driver_wrapper::Kernel surface_kernel_;
};

} // namespace renderer::image_processing