#pragma once

#include <stdexcept>

namespace renderer::image_processing
{

enum class ToneMapping { 
    clip 
};

// 伝達関数
enum class TransferFunction {
    srgb
};

inline constexpr float minimum_exposure_ev = -32.0f;
inline constexpr float maximum_exposure_ev = 32.0f;

class ExposureEv
{
public:
    constexpr ExposureEv() noexcept = default;

    explicit constexpr ExposureEv(double ev)
    {
        // float に丸める前に検査する．NaN と Inf もこの区間に入らない
        if (!(minimum_exposure_ev <= ev && ev <= maximum_exposure_ev))
            throw std::invalid_argument("exposure_ev must be finite and in [-32, 32]");

        value_ = static_cast<float>(ev);
    }

    [[nodiscard]] constexpr float value() const noexcept { return value_; }
    bool operator==(const ExposureEv&) const = default;

private:
    float value_ = 0.0f;
};

struct OutputTransformSettings
{
    ExposureEv exposure_ev{};
    ToneMapping tone_mapping = ToneMapping::clip;
    TransferFunction transfer_function = TransferFunction::srgb;

    bool operator==(const OutputTransformSettings&) const = default;
};


inline void validate_output_transform_settings(
    const OutputTransformSettings& settings
)
{
    // C++ から直接指定された列挙値が，カーネルで使えるものかどうか確認する
    if(settings.tone_mapping != ToneMapping::clip)
    {
        throw std::invalid_argument("Unsupported tone mapping operator");
    }

    if(settings.transfer_function != TransferFunction::srgb)
    {
        throw std::invalid_argument("Unsupported transfer function");
    }
}

} // namespace renderer::image_processing