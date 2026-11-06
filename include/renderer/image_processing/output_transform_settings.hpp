#pragma once

namespace renderer::image_processing
{
enum class ToneMapping { clip };

struct OutputTransformSettings
{
    float exposure_ev = 0.0f;
    ToneMapping tone_mapping = ToneMapping::clip;
};

void validate_output_transform_settings(const OutputTransformSettings& settings);

} // namespace renderer::image_processing