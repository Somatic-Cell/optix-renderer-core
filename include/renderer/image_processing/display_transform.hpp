#pragma once

#include <renderer/core/image.hpp>

namespace renderer::image_processing
{

[[nodiscard]] SRGB8Image linear_to_srgb8(const LinearRGBImage& image);

} // namespace renderer::image_processing