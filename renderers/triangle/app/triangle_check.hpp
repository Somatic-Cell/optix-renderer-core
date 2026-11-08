#pragma once

#include <renderer/gpu/image_view.hpp>

namespace renderer::app
{

// Fixed M1 diagnostic for the 800 x 600 triangle scene. The caller must wait
// for rendering to finish before this synchronous, eight-pixel readback.
void verify_triangle(gpu::ConstLinearRGBImageView image);

} // namespace renderer::app
