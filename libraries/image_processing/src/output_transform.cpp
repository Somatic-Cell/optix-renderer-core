#include <renderer/image_processing/output_transform.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace renderer::image_processing
{
namespace cdw = cuda_driver_wrapper;

namespace {

    std::size_t validate(
    gpu::ConstRGB32fImageView input, 
    CUcontext context,
    const cdw::Stream& stream, 
    const OutputTransformSettings& settings
)
{
    validate_output_transform_settings(settings);
    if(!stream.is_valid() || stream.context() != context)
    {
        throw std::invalid_argument("Output transform stream has the wrong context");
    }
    return validate_image_view(input, context);
}

cdw::LaunchConfig launch_for(
    std::uint32_t width,
    std::uint32_t height
)
{
    cdw::LaunchConfig launch;
    launch.block_x = 16;
    launch.block_y = 16;
    launch.grid_x = 1 + (width - 1) / launch.block_x;
    launch.grid_y = 1 + (height - 1) / launch.block_y;
    return launch;
}

} // namespace

std::filesystem::path output_transform_module_path(const std::filesystem::path& module_directory)
{
    return module_directory / u8"" RENDERER_OUTPUT_TRANSFORM_MODULE_PATH;
}

OutputTransform::OutputTransform(
    CUcontext context,
    const std::filesystem::path& module_path
) : module_(context, module_path),
    linear_kernel_(cdw::get_kernel(module_, "renderer_output_transform_rgb8")), 
    surface_kernel_(cdw::get_kernel(module_, "renderer_output_transform_rgb8_surface")) 
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
    const auto input_bytes = validate(input, context(), stream, settings);

    if(input.width != output.width || input.height != output.height)
    {
        throw std::invalid_argument("Output transform image dimensions do not match");
    }

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


    cdw::launch_kernel(linear_kernel_, launch_for(width, height), stream, arguments);

}

void OutputTransform::enqueue(
    gpu::ConstRGB32fImageView input,
    gpu::RGBA8SurfaceView output,
    const cdw::Stream& stream,
    const OutputTransformSettings& settings
) const
{
    // 入力値の検査
    static_cast<void>(validate(input, context(), stream, settings));
    
    if(
        output.context != context()
        || output.surface == 0
        || input.width != output.width 
        || input.height != output.height
    )
    {
        throw std::invalid_argument("Invalid output RGBA8 surface view");
    }


    if(
        output.width > static_cast<std::uint32_t>((std::numeric_limits<int>::max)() / 4)
        || output.height > static_cast<std::uint32_t>((std::numeric_limits<int>::max)())
    )
    {
        throw std::invalid_argument("Surface dimensions exceed CUDA coordinate range");
    }

    CUdeviceptr source = input.memory.data;
    std::uint64_t source_stride = input.row_stride_bytes;
    CUsurfObject surface = output.surface;
    std::uint32_t width = input.width;
    std::uint32_t height = input.height;
    float exposure_scale = std::exp2(settings.exposure_ev);
    auto arguments = std::to_array<void*>({
        &source,
        &source_stride,
        &surface,
        &width,
        &height,
        &exposure_scale
    });

    cdw::launch_kernel(surface_kernel_, launch_for(width, height), stream, arguments);

}

} // namespace renderer::image_processing