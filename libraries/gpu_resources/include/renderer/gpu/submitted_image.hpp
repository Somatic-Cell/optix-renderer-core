#pragma once

#include <renderer/gpu/image_view.hpp>
#include <cuda_driver_wrapper/event.hpp>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

namespace renderer::gpu
{

template<class Pixel>
class SubmittedImage final
{
public:
    SubmittedImage(
        ConstImageView<Pixel> image,
        std::shared_ptr<const cuda_driver_wrapper::Event> completion,
        std::shared_ptr<const void> conetxt_lifetime,
        std::uint64_t sequence
    ) : 
    image_(image), conetxt_lifetime_(std::move(conetxt_lifetime)),
    completion_(std::move(completion)), sequence_(sequence)
    {
        if(
            !completion_ 
            || !conetxt_lifetime_ 
            || !completion_->is_valid()
            || completion_->context() != image.memory.context
        )
        {
            throw std::invalid_argument("Invalid submitted image lifetime / event");
        }

        static_cast<void>(validate_image_view(image_, completion_->context()));
    }

    [[nodiscard]] 
    ConstImageView<Pixel>image() const noexcept 
    { 
        return image_; 
    }

    [[nodiscard]] 
    const cuda_driver_wrapper::Event& completion_event() const noexcept
    {
        return *completion_;
    }

    [[nodiscard]]
    bool is_ready() const
    {
        return cuda_driver_wrapper::is_event_ready(*completion_);
    }

    [[nodiscard]]
    std::uint64_t sequence() const noexcept
    {
        return sequence_;
    }

    [[nodiscard]]
    const std::shared_ptr<const void>& context_lifetime() const noexcept
    {
        return conetxt_lifetime_;
    }

private:
    ConstImageView<Pixel> image_;
    std::shared_ptr<const void> conetxt_lifetime_;

    std::shared_ptr<const cuda_driver_wrapper::Event> completion_;
    std::uint64_t sequence_;
};

using SubmittedRGB32fImage = SubmittedImage<RGB32f>;

} // namespace renderer::gpu