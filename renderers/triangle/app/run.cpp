#include "run.hpp"
#include "triangle_check.hpp"
#include <renderer/image_processing/output_transform.hpp>
#include <renderer/image_export/export_settings.hpp>
#include <renderer/image_export/rgb8_export_job.hpp>
#include <renderer/triangle/render_session.hpp>
#include <renderer/app_support/gpu_device.hpp>
#include <cuda_driver_wrapper/stream.hpp>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace renderer::triangle
{
namespace cdw = cuda_driver_wrapper;
namespace
{
void render_triangle(const RenderConfig& config, CUdevice device,
    const std::filesystem::path& triangle_module, const std::filesystem::path& output_module)
{
    SessionOptions options;
    options.extent = config.renderer.extent;
    options.frame_slots = 1;
    options.optix_validation = config.device.optix_validation;
    options.diagnose_unwritten_pixels = config.diagnostics.verify_triangle;
    RenderSession session(device, triangle_module, options);
    cdw::Stream output_stream(session.context());
    image_processing::OutputTransform transform(session.context(), output_module);
    if (!session.try_advance()) throw std::logic_error("First render has no free slot");
    auto frame = session.take_latest_image();
    if (!frame) throw std::logic_error("Submitted triangle render has no image");
    image_export::RGB8ExportJob export_job(transform, output_stream,
        config.renderer.extent.width, config.renderer.extent.height);
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
int run(const RenderConfig& config, const std::filesystem::path& module_directory)
{
    const auto triangle_module = module_path(module_directory);
    const auto output_module = image_processing::output_transform_module_path(module_directory);
    if (!std::filesystem::is_regular_file(triangle_module) || !std::filesystem::is_regular_file(output_module))
        throw std::runtime_error("GPU module file not found; build triangle_cli or specify --modules DIRECTORY");
    const auto device = app_support::initialize_cli_device(config.device, module_directory, std::cout);
    render_triangle(config, device, triangle_module, output_module);
    std::cout << "Saved: " << std::filesystem::absolute(config.output.path) << " ("
        << config.renderer.extent.width << " x " << config.renderer.extent.height
        << ", RGB8, exposure EV: " << config.output.transform.exposure_ev.value() << ")\n";
    return EXIT_SUCCESS;
}
}
