#include "run.hpp"
#include "triangle_check.hpp"

#include <renderer/image_processing/output_transform.hpp>
#include <renderer/optix_backend/device_context.hpp>
#include <renderer/image_export/export_settings.hpp>
#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/render_job.hpp>

#include <cuda_driver_wrapper/context.hpp>
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
namespace
{

namespace cdw = cuda_driver_wrapper;

void render_triangle(
    const config::RenderConfig& config,
    CUdevice device,
    const std::filesystem::path& triangle_module,
    const std::filesystem::path& output_module)
{
    // Reverse destruction keeps both GPU consumers and CPU encoding within
    // the lifetimes of their borrowed buffers, modules, streams and contexts.
    cdw::RetainPrimaryContext cuda_context(device);
    optix_backend::DeviceContext optix_context(
        cuda_context.handle(), config.device.optix_validation);
    cdw::Stream render_stream(cuda_context.handle());
    cdw::Stream output_stream(cuda_context.handle());
    triangle::RenderJob render(
        optix_context, render_stream, triangle_module,
        {config.renderer.width, config.renderer.height});
    image_processing::OutputTransform transform(cuda_context.handle(), output_module);
    image_export::RGB8ExportJob export_job(
        transform, output_stream, config.renderer.width, config.renderer.height);

    render.submit();
    // The output stream waits on the producer event on the GPU. The CPU does
    // not wait for rendering before it submits conversion and export_job.
    export_job.submit(render.image(), render.completion_event(), config.output.transform);
    export_job.wait();
    const auto image = export_job.view();

    if (config.diagnostics.verify_triangle)
    {
        // Optional diagnostic: copy only eight linear pixels (96 bytes).
        // export_job.wait() above also guarantees that rendering has completed.
        verify_triangle(render.image(), image, config.output.transform);
    }

    if (!config.output.path.parent_path().empty())
    {
        std::filesystem::create_directories(config.output.path.parent_path());
    }
    image_export::write_export_image(config.output, image);
}

} // namespace

int run(
    const config::RenderConfig& config,
    const std::filesystem::path& module_directory)
{
    image_export::validate_export_request(
        config.output, config.renderer.width, config.renderer.height);

    const auto triangle_module = module_directory / "triangle" / "programs.optixir";
    const auto output_module = module_directory / "image_processing" / "output_transform.fatbin";
    if (!std::filesystem::is_regular_file(triangle_module)
        || !std::filesystem::is_regular_file(output_module))
    {
        throw std::runtime_error(
            "GPU module file not found; build triangle_cli or specify --modules DIRECTORY");
    }

    std::cout << "OptiX header version: " << OPTIX_VERSION << '\n'
              << "CUDA header version: " << CUDA_VERSION << '\n'
              << "Module directory: " << module_directory << '\n';
    cdw::initialize_driver();
    if (config.device.ordinal < 0 || config.device.ordinal >= cdw::get_device_count())
    {
        throw std::runtime_error("Requested CUDA device is unavailable");
    }
    const CUdevice device = cdw::get_device(config.device.ordinal);
    const auto info = cdw::get_device_info(device);
    int driver_version = 0;
    cdw::check_cuda(cuDriverGetVersion(&driver_version), "cuDriverGetVersion");
    std::cout << "GPU: " << info.name << " (compute capability "
              << info.compute_capability_major << '.' << info.compute_capability_minor << ")\n"
              << "CUDA driver API version: " << driver_version << '\n'
              << "OptiX validation: "
              << (config.device.optix_validation ? "enabled" : "disabled") << '\n';

    render_triangle(config, device, triangle_module, output_module);
    std::cout << "Saved: " << std::filesystem::absolute(config.output.path)
              << " (" << config.renderer.width << " x " << config.renderer.height
              << ", RGB8, exposure EV: "
              << config.output.transform.exposure_ev << ")\n";
    return EXIT_SUCCESS;
}

} // namespace renderer::app
