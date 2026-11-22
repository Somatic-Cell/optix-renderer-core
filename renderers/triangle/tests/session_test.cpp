#include <renderer/triangle/render_session.hpp>
#include <renderer/render_execution/render_execution.hpp>
#include <cuda_driver_wrapper/device.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <memory>
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

void check_headless_execution(CUdevice device)
{
    using Session = renderer::triangle::RenderSession;
    renderer::triangle::SessionOptions options;
    options.frame_slots = 1;
    options.optix_validation = true;
    const auto module = renderer::triangle::module_path(u8"" RENDERER_MODULE_DIRECTORY);
    auto image = [&]
    {
        renderer::render_execution::RenderExecution<Session> execution(
            [=](const renderer::task_execution::TaskContext&)
            { return std::make_unique<Session>(device, module, options); }, 3);
        // 最後にだけ画像を取得する場合，1 個のスロットで 3 回の描画を完遂できることを確かめる．
        return execution.wait_take_result();
    }();
    require(image.sequence() == 3, "Headless batch did not submit three renders");
    cuda_driver_wrapper::synchronize_event(image.completion_event());
    check_pixel(image);
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
                renderer::triangle::module_path(u8"" RENDERER_MODULE_DIRECTORY), options);
            require(!session.take_latest_image(), "Published an image before any launch");
            require(session.try_advance(), "First advance was rejected");
            require(session.try_advance(), "Advancing required taking the previous image");
            auto first = session.take_latest_image();
            require(first && first->sequence() == 2, "Did not publish the newest submission");
            require(!session.take_latest_image(), "Published the same submission twice");
            require(session.statistics().render_launches == 2, "Taking an image launched work");
            // 2 回の描画は同じストリームに投入される．後の描画の完了を待てば，
            // 先の描画も完了し，画像ハンドルが保持していないスロットを再利用できる．
            cdw::synchronize_event(first->completion_event());
            require(session.try_advance(), "Could not advance with a free slot");
            auto second = session.take_latest_image();
            require(second && second->sequence() == 3, "Third submission missing");
            cdw::synchronize_event(second->completion_event());
            check_pixel(*first); check_pixel(*second);
            require(!session.try_advance(), "Leased slot was overwritten");
            require(!session.take_latest_image(), "A rejected advance produced an image");
            const auto old_address = first->image().memory.data;
            auto consumer_lease = first;
            first.reset();
            require(!session.try_advance(), "Secondary consumer lease was ignored");
            consumer_lease.reset();
            require(session.try_advance(), "Retry failed after releasing a slot");
            auto third = session.take_latest_image();
            require(third && third->sequence() == 4, "Retry produced the wrong submission");
            require(third->image().memory.data == old_address, "Released slot not reused");
            cdw::synchronize_event(third->completion_event());
            check_pixel(*third);
            const auto counters = session.statistics();
            require(counters.pipeline_builds == 1 && counters.gas_builds == 1
                && counters.geometry_uploads == 1 && counters.sbt_uploads == 1
                && counters.parameter_uploads == 2 && counters.frame_allocations == 2
                && counters.render_launches == 4 && counters.diagnostic_clears == 0,
                "Repeated render rebuilt/reallocated/reuploaded/cleared resources");
            session.wait_idle();
            require(!session.collect(), "Completed setup staging was not retired");
            retained = std::move(third);
        }
        // セッションを破棄した後も，画像ハンドルを通じて完了イベントと画素にアクセスできる．
        require(retained->is_ready(), "Frame event did not survive session");
        check_pixel(*retained);
        retained.reset();
        check_headless_execution(cdw::get_device(0));
        std::cout << "Triangle session: PASS (direct session and autonomous headless batch)\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
