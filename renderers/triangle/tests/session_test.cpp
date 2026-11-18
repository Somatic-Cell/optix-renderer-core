#include <renderer/render_session.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY is required
#endif
namespace
{
void require(bool value, const char* message)
{ if (!value) throw std::runtime_error(message); }
void check_pixel(const renderer::gpu::SubmittedRGB32fImage& frame)
{
    const auto image = frame.image();
    renderer::RGB32f pixel{};
    const std::uint32_t x = image.width / 2, y = image.height / 2;
    cuda_driver_wrapper::copy_device_to_host(std::as_writable_bytes(std::span(&pixel, 1)),
        cuda_driver_wrapper::subview(image.memory, y * image.row_stride_bytes + x * sizeof(pixel), sizeof(pixel)));
    const double px = (2.0 * (x + 0.5) / image.width - 1.0) * image.width / image.height;
    const double py = 1.0 - 2.0 * (y + 0.5) / image.height;
    const double blue = (py + 0.65) / 1.4;
    const double green = 0.5 * (1.0 - blue) + px / 1.5;
    require(std::isfinite(pixel.r) && std::isfinite(pixel.g) && std::isfinite(pixel.b), "Nonfinite center");
    require(std::abs(pixel.r - (1.0 - green - blue)) < 5e-5
        && std::abs(pixel.g - green) < 5e-5 && std::abs(pixel.b - blue) < 5e-5, "Wrong center pixel");
}
}
int main()
{
    try
    {
        namespace cdw = cuda_driver_wrapper;
        cdw::initialize_driver();
        if (cdw::get_device_count() == 0) throw std::runtime_error("A CUDA GPU is required");
        std::optional<renderer::gpu::SubmittedRGB32fImage> retained;
        {
            renderer::triangle::SessionOptions options;
            options.frame_slots = 2;
            options.optix_validation = true;
            renderer::triangle::RenderSession session(cdw::get_device(0),
                std::filesystem::path(RENDERER_MODULE_DIRECTORY) / "triangle" / "programs.optixir", options);
            auto first = session.try_render(), second = session.try_render();
            require(first.has_value() && second.has_value(), "Initial slots unavailable");
            cdw::synchronize_event(first->completion_event());
            cdw::synchronize_event(second->completion_event());
            check_pixel(*first); check_pixel(*second);
            require(!session.try_render(), "Leased slot was overwritten");
            const auto old_address = first->image().memory.data;
            auto consumer_lease = first;
            first.reset();
            require(!session.try_render(), "Secondary consumer lease was ignored");
            consumer_lease.reset();
            auto third = session.try_render();
            require(third.has_value() && third->image().memory.data == old_address, "Slot not reused");
            cdw::synchronize_event(third->completion_event());
            check_pixel(*third);
            const auto counters = session.statistics();
            require(counters.pipeline_builds == 1 && counters.gas_builds == 1
                && counters.geometry_uploads == 1 && counters.sbt_uploads == 1
                && counters.parameter_uploads == 2 && counters.frame_allocations == 2
                && counters.render_launches == 3 && counters.diagnostic_clears == 0,
                "Repeated render rebuilt/reallocated/reuploaded/cleared resources");
            session.wait_idle();
            require(!session.collect(), "Completed setup staging was not retired");
            retained = std::move(third);
        }
        // No session/pipeline/GAS is retained. Only pixels/event/context survive.
        require(retained->is_ready(), "Frame event did not survive session");
        check_pixel(*retained);
        retained.reset();
        std::cout << "Triangle session: PASS (3 launches, 1 pipeline/GAS, 2 slots, no per-frame uploads/clears)\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
