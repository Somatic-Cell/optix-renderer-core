#include <renderer/image_processing/output_transform.hpp>
#include <renderer/image_types/pixel.hpp>

#include <cuda_driver_wrapper/stream.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace renderer::image_processing
{
namespace cdw = cuda_driver_wrapper;


OutputTransform::OutputTransform(
    CUcontext context,
    const std::filesystem::path& module_path
) : module_(context, module_path),
    kernel_(cdw::get_kernel(module_, "renderer_output_transform_rgb8"))   // 関数名
{
}

void OutputTransform::enqueue(
    gpu::ConstRGB32fImageView input,
    gpu::RGB8ImageView output,
    const cdw::Stream& stream,
    const OutputTransformSettings& settings
) const
{
    // 入力値の検査
    validate_output_transform_settings(settings);
    if(!stream.is_valid() || stream.context() != context())
    {
        throw std::invalid_argument("Output transform stream has the wrong context");
    }

    if(input.width != output.width || input.height != output.height)
    {
        throw std::invalid_argument("Output transform image dimensions do not match");
    }

    const auto input_bytes = gpu::validate_image_view(input, context());
    const auto output_bytes = gpu::validate_image_view<RGB8>(output, context());

    if(
        input.memory.data < output.memory.data + output_bytes
        && output.memory.data < input.memory.data + input_bytes)
    {
        throw std::invalid_argument("Output transform view must not overlap");
    }

    CUdeviceptr source = input.memory.data;
    CUdeviceptr destination = output.memory.data;
    std::uint64_t source_stride = input.row_stride_bytes;
    std::uint64_t destination_stride = output.row_stride_bytes;
    std::uint32_t width = input.width;
    std::uint32_t height = input.height;
    float exposure_scale = std::exp2(settings.exposure_ev);
    auto arguments = std::to_array<void*>({
        &source,
        &source_stride,
        &destination,
        &destination_stride,
        &width,
        &height,
        &exposure_scale
    });

    cdw::LaunchConfig launch;
    launch.block_x = 16;
    launch.block_y = 16;
    launch.grid_x = 1 + (width - 1) / launch.block_x;
    launch.grid_y = 1 + (height - 1) / launch.block_y;

    cdw::launch_kernel(kernel_, launch, stream, arguments);

}

} // namespace renderer::image_processing