#pragma once

#include <renderer/image_types/image.hpp>
#include <renderer/gpu/image_view.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>

namespace renderer::app
{

// Fixed M1 diagnostic for the 800 x 600 triangle scene. The caller must wait
// for rendering to finish before this synchronous, eight-pixel readback.
void verify_triangle(
    gpu::ConstRGB32fImageView image,
    RGB8ImageView readback,
    const image_processing::OutputTransformSettings& settings
);

} // namespace renderer::app
