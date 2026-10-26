#include "launch_params.hpp"

#include <optix.h>
#include <vector_functions.h>

extern "C"
{
__constant__ renderer::triangle::LaunchParams params;
}

extern "C" __global__ void __raygen__triangle()
{
    const uint3 index = optixGetLaunchIndex();
    const float aspect = static_cast<float>(params.width) / static_cast<float>(params.height);
    const float x = (2.0f * (static_cast<float>(index.x) + 0.5f)
                     / static_cast<float>(params.width) - 1.0f) * aspect;
    const float y = 1.0f - 2.0f * (static_cast<float>(index.y) + 0.5f)
                              / static_cast<float>(params.height);

    // Orthographic camera. Image row zero is the top, not the bottom.
    unsigned int r = 0;
    unsigned int g = 0;
    unsigned int b = 0;
    optixTrace(
        params.traversable,
        make_float3(x, y, 1.0f), make_float3(0.0f, 0.0f, -1.0f),
        0.0f, 1.0e16f, 0.0f, OptixVisibilityMask(255),
        OPTIX_RAY_FLAG_DISABLE_ANYHIT, 0, 1, 0, r, g, b);

    const auto offset = static_cast<unsigned long long>(index.y) * params.width + index.x;
    params.pixels[offset] = {__uint_as_float(r), __uint_as_float(g), __uint_as_float(b)};
}

extern "C" __global__ void __miss__background()
{
    optixSetPayload_0(__float_as_uint(0.02f));
    optixSetPayload_1(__float_as_uint(0.03f));
    optixSetPayload_2(__float_as_uint(0.05f));
}

extern "C" __global__ void __closesthit__triangle()
{
    const float2 barycentric = optixGetTriangleBarycentrics();
    optixSetPayload_0(__float_as_uint(1.0f - barycentric.x - barycentric.y));
    optixSetPayload_1(__float_as_uint(barycentric.x));
    optixSetPayload_2(__float_as_uint(barycentric.y));
}
