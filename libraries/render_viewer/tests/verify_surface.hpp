#pragma once
#include <renderer/gpu/submitted_image.hpp>
#include <renderer/image_processing/output_transform.hpp>
namespace renderer::render_viewer::tests
{
void verify_surface(unsigned int texture, const gpu::SubmittedRGB32fImage& source,
    const image_processing::OutputTransform& transform,
    const image_processing::OutputTransformSettings& settings);
}
