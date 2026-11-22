#include "run.hpp"
#include "triangle_check.hpp"
#include <renderer/image_processing/output_transform.hpp>
#include <renderer/image_export/export_settings.hpp>
#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/triangle/render_session.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/error.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <optix.h>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace renderer::app
{
namespace cdw = cuda_driver_wrapper;
namespace
{
void render_triangle(const config::RenderConfig& config, CUdevice device,
    const std::filesystem::path& triangle_module, const std::filesystem::path& output_module)
{
    triangle::SessionOptions options;
    options.image = {config.renderer.width, config.renderer.height};
    options.frame_slots = 1;
    options.optix_validation = config.device.optix_validation;
    options.diagnose_unwritten_pixels = config.diagnostics.verify_triangle;
    triangle::RenderSession session(device, triangle_module, options);
    cdw::Stream output_stream(session.context());
    image_processing::OutputTransform transform(session.context(), output_module);
    if (!session.try_advance()) throw std::logic_error("First render has no free slot");
    auto frame = session.take_latest_image();
    if (!frame) throw std::logic_error("Submitted triangle render has no image");
    image_export::RGB8ExportJob export_job(transform, output_stream,
        config.renderer.width, config.renderer.height);
    export_job.submit(frame->image(), frame->completion_event(), config.output.transform);
    export_job.wait();
    const auto image = export_job.view();
    if (config.diagnostics.verify_triangle)
        verify_triangle(frame->image(), image, config.output.transform);
    if (!config.output.path.parent_path().empty())
        std::filesystem::create_directories(config.output.path.parent_path());
    image_export::write_export_image(config.output, image);
}
}
int run(const config::RenderConfig& config, const std::filesystem::path& module_directory)
{
    image_export::validate_export_request(config.output, config.renderer.width, config.renderer.height);
    const auto triangle_module = module_directory / "triangle" / "programs.optixir";
    const auto output_module = module_directory / "image_processing" / "output_transform.fatbin";
    if (!std::filesystem::is_regular_file(triangle_module) || !std::filesystem::is_regular_file(output_module))
        throw std::runtime_error("GPU module file not found; build triangle_cli or specify --modules DIRECTORY");
    std::cout << "OptiX header version: " << OPTIX_VERSION << '\n'
              << "CUDA header version: " << CUDA_VERSION << '\n'
              << "Module directory: " << module_directory << '\n';
    cdw::initialize_driver();
    if (config.device.ordinal < 0 || config.device.ordinal >= cdw::get_device_count())
        throw std::runtime_error("Requested CUDA device is unavailable");
    const auto device = cdw::get_device(config.device.ordinal);
    const auto info = cdw::get_device_info(device);
    int version = 0;
    cdw::check_cuda(cuDriverGetVersion(&version), "cuDriverGetVersion");
    std::cout << "GPU: " << info.name << " (compute capability "
        << info.compute_capability_major << '.' << info.compute_capability_minor << ")\n"
        << "CUDA driver API version: " << version << '\n'
        << "OptiX validation: " << (config.device.optix_validation ? "enabled" : "disabled") << '\n';
    render_triangle(config, device, triangle_module, output_module);
    std::cout << "Saved: " << std::filesystem::absolute(config.output.path) << " ("
        << config.renderer.width << " x " << config.renderer.height
        << ", RGB8, exposure EV: " << config.output.transform.exposure_ev << ")\n";
    return EXIT_SUCCESS;
}
}
