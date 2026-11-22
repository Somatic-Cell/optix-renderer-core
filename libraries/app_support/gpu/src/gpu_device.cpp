#include <renderer/app_support/gpu_device.hpp>

#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/error.hpp>
#include <optix.h>

#include <ostream>
#include <stdexcept>

namespace renderer::app_support
{

CUdevice initialize_cli_device(
    const DeviceSettings& settings,
    const std::filesystem::path& module_directory,
    std::ostream& log
)
{
    namespace cdw = cuda_driver_wrapper;

    log << "OptiX header version: " << OPTIX_VERSION << '\n'
        << "CUDA header version: " << CUDA_VERSION << '\n'
        << "Module directory: " << module_directory << '\n';

    cdw::initialize_driver();

    if(
        settings.ordinal < 0                            // GPU が存在しない
        || settings.ordinal >= cdw::get_device_count()  // 使用したい GPU のスロットが使用可能な枚数より多い
    )
    {
        throw std::runtime_error("Requested CUDA device is unavailable");
    }

    const auto device = cdw::get_device(settings.ordinal);
    const auto info = cdw::get_device_info(device);

    int version = 0;

    cdw::check_cuda(
        cuDriverGetVersion(&version),
        "cuDriverGetVersion"
    );

    log << "GPU: " << info.name << " (compute capability " 
        << info.compute_capability_major << '.' << info.compute_capability_minor
        << "CUDA driver API version: " << version << '\n'
        << "OptiX validation: " << (settings.optix_validation ? "enabled" : "disabled") << '\n';

    return device;
}

} // namespace renderer::app_support