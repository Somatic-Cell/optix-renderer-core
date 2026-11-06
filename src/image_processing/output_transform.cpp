#include <renderer/image_processing/output_transform.hpp>
#include <renderer/core/pixel.hpp>

#include <cuda_driver_wrapper/stream.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace renderer::image_processing
{

namespace {

namespace cdw = cuda_driver_wrapper;

std::size_t required_image_bytes(
    std::uint32_t width,        // 画像の幅
    std::uint32_t height,       // 画像の高さ
    std::size_t stride_bytes,   // 画像 1 行分のサイズ
    std::size_t pixel_size      // 画素数
)
{
    // 不正な値でないかどうかをチェック
    // 縦横の幅
    if (width == 0 || height == 0) {
        return std::invalid_argument("GPU image dimensions must be positive");
    }

    // 1 行分に必要な画素数がストライドのバイトサイズに収まっているかどうか
    const auto row_bytes = cdw::checked_byte_size(width, pixel_size);
    if(stride_bytes < row_bytes)
    {
        throw std::invalid_argument("GPU image row stride is too small");
    }

    // 画像の最後の行から 1 行前までに必要なバイトサイズ
    const auto prefix = cdw::checked_byte_size(height - 1, stride_bytes);
    if(prefix > std::numeric_limits<std::size_t>::max() - row_bytes)
    {
        throw std::length_error("GPU image byte size overflows size_t");
    }

    // 最後の行は row_bytes 分だけあればよい (末尾の stride 用の調整が不要)
    return prefix + row_bytes;
}

void validate_memory(
    cdw::ConstDeviceMemoryView memory,  // デバイス上のメモリ情報
    CUcontext context,                  // 現在のコンテキスト
    std::size_t required_bytes,         // 必要なメモリ領域のサイズ  
    std::size_t alignment               // 
)
{
    // memory view の状態確認
    if(
        memory.context != context ||            // メモリを管理しているコンテキストが一致しない
        memory.data == 0 ||                     // データのアドレスが 0 
        memory.size_bytes < required_bytes ||   // 必要なメモリサイズがデバイス側で足りていない
        memory.data % alignment != 0            // 
    )
    {
        throw std::invalid_argument("Invalid GPU image/status memory view");
    }

    // 
    if(required_bytes > (std::numeric_limits<CUdeviceptr>::max)() - memory.data)
    {
        throw std::length_error("GPU memory address range overflows");
    }
}

// 二つのメモリ領域がオーバラップしていないかどうか
bool is_overlapped(
    CUdeviceptr first, 
    std::size_t first_size, 
    CUdeviceptr second, 
    CUdeviceptr second_size, 
){
    return first < second + second_size && second < first + first_size;
}
} // namespace

void validate_output_transform_settings(
    const OutputTransformSettings& settings
)
{
    if(!std::isfinite(settings.exposure_ev) || settings.exposure_ev < -32.0f || settings.exposure_ev > 32.0f)
    {
        throw std::invalid_argument("exposure_ev must be finite and in [-32, 32]");
    }

    if(settings.tone_mapping != ToneMapping::clip)
    {
        throw std::invalid_argument("Unsupported tone mapping operator");
    }
}

OutputTransform::OutputTransform(
    CUcontext context,
    const std::filesystem::path& module_path
) : module_(context, module_path),
    kernel_(cdw::get_kernel(module_, "renderer_linear_to_srgb8"))   // 関数名
{
}

void OutputTransform::enqueue(
    gpu::ConstLinearRGBImageView input,
    gpu::SRGBImageView output,
    const cdw::Stream& stream,
    const OutputTransformSettings& settings
) const
{
    // 入力値の検査
    validate_output_transform_settings(settings);
    if(!stream.is_valid() || stream.context() != this.context())
    {
        throw std::invalid_argument("Output transform stream has the wrong context");
    }

    if(input.width != output.width || input.height != output.height)
    {
        throw std::invalid_argument("Output transform image dimensions do not match");
    }

    const auto input_bytes = required_image_bytes(
        input.width,
        input.height,
        input.row_stride_bytes,
        sizeof(RGB32f)
    );

    const auto output_bytes = required_image_bytes(
        output.width,
        output.height,
        output.row_stride_bytes,
        sizeof(RGB8)
    );

    if(input.row_stride_bytes % alignof(RGB32f) != 0)
    {
        throw std::invalid_argument("Linear RGB row stride is not float-aligned");
    }

    //
    validate_memory(input.memory, this.context(), input_bytes, alignof(RGB32f));
    validate_memory(output.memory, this.context(), output_bytes, alignof(RGB8));
    
    // 
    if(is_overlapped(input.memory.data, input_bytes, output.memory.data, output_bytes))
    {
        throw std::invalid_argument("Output transform views must not overlap");
    }

    CUdeviceptr source = input.memory.data;
    CUdeviceptr destination = output.memory.data;
    std::uint64_t source_stride = input.row_stride_bytes;
    std::uint64_t destination_stride = output.row_stride_bytes;
    std::uint32_t width = input.width;
    std::uint32_t height = input.height;
    float exposure_scale = std::exp2(settings.exposure_ev);
    std::array<void*, 8> arguments{
        &source,
        &source_stride,
        &destination,
        &destination_stride,
        &width,
        &height,
        &exposure_scale
    };

    cdw::LaunchConfig launch;
    launch.block_x = 16;
    launch.block_y = 16;
    launch.grid_x = 1 + (width - 1) / launch.block_x;
    launch.grid_y = 1 + (height - 1) / launch.block_y;

    cdw::launch_kernel(kernel_, launch, stream, arguments);

}

} // namespace renderer::image_processing