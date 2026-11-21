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
        std::shared_ptr<const void > pixels_lifetime,
        std::shared_ptr<const cuda_driver_wrapper::Event> completion,
        std::shared_ptr<const void> conetxt_lifetime,
        std::uint64_t sequence
    ) : 
    image_(image), 
    context_lifetime_(std::move(conetxt_lifetime)),
    pixels_lifetime_(std::move(pixels_lifetime)),
    completion_(std::move(completion)), 
    sequence_(sequence)
    {
        if(
            !pixels_lifetime_
            || pixels_lifetime_.use_count() == 0
            || !completion_ 
            || completion_.use_count() == 0 
            || !context_lifetime_ 
            || context_lifetime_.use_count() == 0 
        )
        {
            throw std::invalid_argument("Submitted image requires owing pixel, event and context lifetime");
        }

        if (!completion_->is_valid() || completion_->context() != image.memory.context)
        {
            throw std::invalid_argument("Invalid submitted image event / context");
        }

        static_cast<void>(validate_image_view(image_, completion_->context()));
    }

    SubmittedImage(const SubmittedImage&) noexcept = default;
    SubmittedImage(SubmittedImage&&) noexcept = default;

    SubmittedImage& operator=(SubmittedImage other) noexcept
    {
        swap(other);
        return *this;
    }

    void swap(SubmittedImage& other) noexcept
    {
        using std::swap;
        swap(image_, other.image_);
        context_lifetime_.swap(other.context_lifetime_);
        pixels_lifetime_.swap(other.pixels_lifetime_);
        completion_.swap(other.completion_);
        swap(sequence_, other.sequence_);
    }

    friend void swap(SubmittedImage& left, SubmittedImage& right) noexcept
    {
        left.swap(right);
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
        return context_lifetime_;
    }

private:
    ConstImageView<Pixel> image_;

    std::shared_ptr<const void> context_lifetime_;
    std::shared_ptr<const void> pixels_lifetime_;
    std::shared_ptr<const cuda_driver_wrapper::Event> completion_;
    std::uint64_t sequence_;
};

using SubmittedRGB32fImage = SubmittedImage<RGB32f>;

} // namespace renderer::gpu