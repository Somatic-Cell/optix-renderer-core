#pragma once

#include <renderer/core/pixel.hpp>

#include <optix_types.h>

namespace renderer::triangle
{

// Renderer-specific host/device ABI; this is not an application-facing request.
struct LaunchParams
{
    RGB32f* pixels;
    unsigned int width;
    unsigned int height;
    OptixTraversableHandle traversable;
};

} // namespace renderer::triangle
