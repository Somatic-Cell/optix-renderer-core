#pragma once 

#include <renderer/image_types/pixel.hpp>
#include <renderer/image_types/image_layout.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace renderer::gpu
{

// 借用した GPU 上の linear メモリ
template<class Pixel>
struct ConstImageView
{
    static_assert(std::is_trivially_copyable_v<Pixel>);
    cuda_driver_wrapper::ConstDeviceMemoryView memory;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_stride_bytes = 0;
};

template<class Pixel>
struct ImageView
{
    static_assert(std::is_trivially_copyable_v<Pixel>);
    cuda_driver_wrapper::DeviceMemoryView memory;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_stride_bytes = 0;

    [[nodiscard]] operator ConstImageView<Pixel>() const noexcept
    {
        return {memory, width, height, row_stride_bytes};
    }
};

using ConstRGB8ImageView = ConstImageView<RGB8>;
using RGB8ImageView = ImageView<RGB8>;

using ConstRGB32fImageView = ConstImageView<RGB32f>;
using RGB32fImageView = ImageView<RGB32f>;

[[nodiscard]] inline std::size_t validate_image_memory(
    cuda_driver_wrapper::ConstDeviceMemoryView memory,
    CUcontext context,
    std::uint32_t width,
    std::uint32_t height,
    std::size_t stride,
    std::size_t pixel_size,
    std::size_t alignment
)
{
    const auto bytes = renderer::required_image_bytes(width, height, stride, pixel_size);
    if(
        context == nullptr 
        || memory.context != context 
        || memory.data == 0
        || memory.size_bytes < bytes
        || alignment == 0
        || memory.data % alignment != 0
        || stride % alignment != 0
    )
    {
        throw std::invalid_argument("Invalid GPU image memory view");
    }

    if(bytes > (std::numeric_limits<CUdeviceptr>::max)() - memory.data)
    {
        throw std::length_error("GPU image address range overflows");
    }

    return bytes;
}

template<class Pixel>
[[nodiscard]] std::size_t validate_image_view(
    ConstImageView<Pixel> image,
    CUcontext context
)
{
    return validate_image_memory(image.memory, context, image.width, image.height, 
        image.row_stride_bytes, sizeof(Pixel), alignof(Pixel)
    );
}

} // namespace renderer::gpu