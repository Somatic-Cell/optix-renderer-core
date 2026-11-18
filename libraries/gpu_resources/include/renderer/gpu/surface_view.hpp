#pragma once

#include <cuda.h>
#include <cstdint>

namespace renderer::gpu
{

struct RGBA8SurfaceView
{
    CUcontext context       = nullptr;
    CUsurfObject surface    = 0;
    std::uint32_t width     = 0; 
    std::uint32_t height    = 0; 
};

}