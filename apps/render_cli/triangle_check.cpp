#include "triangle_check.hpp"

#include <renderer/core/pixel.hpp>

#include <cuda_driver_wrapper/device_memory.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>

namespace renderer::app
{

void verify_triangle(gpu::ConstLinearRGBImageView image)
{
    namespace cdw = cuda_driver_wrapper;
    if (image.width != 800 || image.height != 600)
    {
        throw std::invalid_argument("Triangle verification requires an 800 x 600 image");
    }
    const auto row_bytes = cdw::checked_byte_size(image.width, sizeof(RGB32f));
    if (image.row_stride_bytes < row_bytes
        || image.row_stride_bytes > (std::numeric_limits<std::size_t>::max)() / image.height
        || image.memory.size_bytes < (image.height - 1) * image.row_stride_bytes + row_bytes)
    {
        throw std::invalid_argument("Invalid GPU image layout for triangle verification");
    }

    struct Probe
    {
        unsigned int x;
        unsigned int y;
        bool inside;
    };
    const std::array<Probe, 8> probes{{
        {0, 0, false},
        {image.width - 1, 0, false},
        {0, image.height - 1, false},
        {image.width - 1, image.height - 1, false},
        {image.width / 2, image.height / 2, true},
        {image.width * 5 / 16, image.height * 3 / 4, true},
        {image.width * 11 / 16, image.height * 3 / 4, true},
        {image.width / 2, image.height / 4, true}
    }};

    double maximum_error = 0.0;
    for (const auto probe : probes)
    {
        RGB32f actual{};
        const auto offset = probe.y * image.row_stride_bytes
            + static_cast<std::size_t>(probe.x) * sizeof(RGB32f);
        cdw::copy_device_to_host(
            std::as_writable_bytes(std::span(&actual, 1)),
            cdw::subview(image.memory, offset, sizeof(RGB32f)));
        if (!std::isfinite(actual.r) || !std::isfinite(actual.g) || !std::isfinite(actual.b))
        {
            throw std::runtime_error("Triangle verification found a non-finite pixel");
        }

        // Double precision is confined to this independent analytic reference;
        // rendering and the GPU output transform use float.
        std::array<double, 3> expected{0.02, 0.03, 0.05};
        if (probe.inside)
        {
            const double aspect = static_cast<double>(image.width) / image.height;
            const double x = (2.0 * (probe.x + 0.5) / image.width - 1.0) * aspect;
            const double y = 1.0 - 2.0 * (probe.y + 0.5) / image.height;
            const double blue = (y + 0.65) / 1.4;
            const double green = 0.5 * (1.0 - blue) + x / 1.5;
            expected = {1.0 - green - blue, green, blue};
        }
        const double error = std::max({
            std::abs(static_cast<double>(actual.r) - expected[0]),
            std::abs(static_cast<double>(actual.g) - expected[1]),
            std::abs(static_cast<double>(actual.b) - expected[2])
        });
        maximum_error = std::max(maximum_error, error);
        if (error > 5.0e-5)
        {
            throw std::runtime_error(
                "Triangle verification failed at pixel (" + std::to_string(probe.x)
                + ", " + std::to_string(probe.y) + ")");
        }
    }
    std::cout << "Triangle check: PASS (8 analytic probes; max error "
              << std::scientific << maximum_error << std::defaultfloat << ")\n";
}

} // namespace renderer::app
