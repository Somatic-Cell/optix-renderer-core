#include <renderer/image_processing/display_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace renderer::image_processing
{

namespace 
{

std::uint8_t encode_channel(float value)
{
    if(!std::isfinite(value)){
        throw std::invalid_argument("Cannot display a non-finite RGB value");
    }

    const double linear = std::clamp(static_cast<double>(value), 0.0, 1.0);
    const double encoded = linear <= 0.0031308
        ? 12.92 * linear
        : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::lround(encoded * 255.0));
}

} // namespace

SRGB8Image linear_to_srgb8(const LinearRGBImage& image)
{
    if ( image.width == 0 || image.height == 0)
    {
        throw std::invalid_argument("Image dimensions must be positive");
    }
    const std::uint64_t count = static_cast<std::uint64_t>(image.width) * image.height;

    if(count > std::numeric_limits<std::size_t>::max() / sizeof(RGB32f))
    {
        throw std::overflow_error("Image byte size does not fit in size_t");
    }

    if(image.pixels.size() != static_cast<size_t>(count))
    {
        throw std::invalid_argument("Linear image dimensions do not match its pixel count");
    }

    SRGB8Image result{image.width, image.height, std::vector<RGB8>(image.pixels.size())};
    for(std::size_t i = 0; i < image.pixels.size(); ++i)
    {
        const RGB32f& source = image.pixels[i];
        result.pixels[i] = {
            encode_channel(source.r),
            encode_channel(source.g),
            encode_channel(source.b)
        };
    }
    return result;
}

} // namespace renderer::image_processing 