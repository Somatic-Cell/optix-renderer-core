#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/gpu/image_readback.hpp>
#include <renderer/image_processing/output_transform.hpp>

#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/stream.hpp>

#include <cstdio>
#include <exception>
#include <stdexcept>

namespace renderer::image_export
{

namespace 
{

namespace cdw = cuda_driver_wrapper;

CUcontext checked_context(
    const image_processing::OutputTransform& transform,
    const cdw::Stream& stream
)
{
    if(!stream.is_valid() || stream.context() != transform.context())
    {
        throw std::invalid_argument("Export stream must match output transform context");
    }
    return transform.context();
}

} // namespace

struct RGB8ExportJob::Impl
{
    const image_processing::OutputTransform& transform;
    cdw::DeviceBuffer device_pixels;
    gpu::RGB8ImageView destination;
    gpu::RGB8Readback readback;

    Impl(
        const image_processing::OutputTransform& transform_in,
        const cdw::Stream& stream,
        std::uint32_t width,
        std::uint32_t height
    ) : transform(transform_in), 
        device_pixels(checked_context(transform, stream), packed_image_bytes(width, height, sizeof(RGB8))),
        destination{device_pixels.view(), width, height, checked_row_bytes(width, sizeof(RGB8))},
        readback(stream, width, height)
    {
    }

};

RGB8ExportJob::RGB8ExportJob(
    const image_processing::OutputTransform& transform,
    const cdw::Stream& stream,
    std::uint32_t width, 
    std::uint32_t height
) : impl_(std::make_unique<Impl>(transform, stream, width, height))
{
}

RGB8ExportJob::~RGB8ExportJob() noexcept = default;

void RGB8ExportJob::submit(
    gpu::ConstRGB32fImageView input,
    const cuda_driver_wrapper::Event& producer_completion,
    const image_processing::OutputTransformSettings& settings
)
{
    if(input.width != impl_->destination.width || input.height != impl_->destination.height)
    {
        throw std::invalid_argument("Export image dimensions do not match");
    }

    // 無効な設定は GPU に処理を投入する前に返す．呼び出し側は修正後に再試行できる
    image_processing::validate_output_transform_settings(settings);

    impl_->readback.submit(impl_->destination, producer_completion, [&](const cdw::Stream& stream)
    {
        impl_->transform.enqueue(input, impl_->destination, stream, settings);
    });
}

bool RGB8ExportJob::is_ready() { return impl_->readback.is_ready(); }
void RGB8ExportJob::wait() { impl_->readback.wait(); }
RGB8ImageView RGB8ExportJob::view() { return impl_->readback.view(); }

} // namespace renderer::output