#pragma once 

#include <cuda_driever_wrapper/device_memory.hpp>

#include <cstddef>
#include <cstdint>

namespace renderer::gpu
{

// 借用した GPU 上の linear メモリ
struct ConstLinearRGBImageView
{
    cuda_driver_wrapper::COnstDeviceMemoryView memory;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_stride_bytes = 0;
};

struct SRGBImageView
{
    cuda_driver_wrapper::DeviceMemoryView memory;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t row_stride_bytes = 0;
};


} // namespace renderer::gpu