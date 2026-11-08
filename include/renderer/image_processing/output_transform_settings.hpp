#pragma once

#include <cmath>
#include <stdexcept>

namespace renderer::image_processing
{

enum class ToneMapping { 
    clip 
};

// 色空間
enum class ColorEncoding {
    sRGB
};

inline constexpr float minimum_exposure_ev = -32.0f;
inline constexpr float maximum_exposure_ev = 32.0f;

struct OutputTransformSettings
{
    float exposure_ev = 0.0f;
    ToneMapping tone_mapping = ToneMapping::clip;
    ColorEncoding color_encoding = ColorEncoding::sRGB;
};

inline void validate_exposure_ev(double exposure_ev)
{
    if(
        !std::isfinite(exposure_ev)
        || exposure_ev < minimum_exposure_ev
        || exposure_ev > maximum_exposure_ev
    )
    {
        throw std::invalid_argument("exposure_ev must be finite and in [-32, 32]");
    }
}

void validate_output_transform_settings(
    const OutputTransformSettings& settings
)
{
    validate_exposure_ev(settings.exposure_ev);

    if(settings.tone_mapping != ToneMapping::clip)
    {
        throw std::invalid_argument("Unsupported tone mapping operator");
    }

    if(settings.color_encoding != ColorEncoding::sRGB)
    {
        throw std::invalid_argument("Unsupported color encoding");
    }
}

} // namespace renderer::image_processing