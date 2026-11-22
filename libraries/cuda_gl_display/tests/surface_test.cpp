#include "verify_surface.hpp"

#include <renderer/cuda_gl_display/sufrece_presenter.hpp>
#include <renderer/image_processing/output_transform.hpp>
#include <renderer/viewer/window.hpp>
#include <renderer/viewer/workspace.hpp>
#include <cuda_driver_wrapper/context.hpp>
#include <cuda_driver_wrapper/device_buffer.hpp>
#include <cuda_driver_wrapper/device_memory.hpp>
#include <imgui.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must point to staged GPU modules
#endif

namespace
{
namespace cdw = cuda_driver_wrapper;
namespace display = renderer::cuda_gl_display;
namespace processing = renderer::image_processing;

constexpr std::uint32_t width = 17;
constexpr std::uint32_t height = 13;
constexpr std::size_t row_stride = width * sizeof(renderer::RGB32f) + 16;

std::vector<std::byte> make_input()
{
    std::vector<std::byte> bytes(row_stride * height);
    for (std::uint32_t y = 0; y < height; ++y)
    {
        for (std::uint32_t x = 0; x < width; ++x)
        {
            const renderer::RGB32f pixel{
                static_cast<float>(x) / static_cast<float>(width - 1),
                static_cast<float>(y) / static_cast<float>(height - 1),
                0.18f};
            std::memcpy(bytes.data() + y * row_stride + x * sizeof(pixel), &pixel, sizeof(pixel));
        }
    }

    // verify_surface が読む 8 点に，通常値・伝達関数の境界・範囲外・非有限値を置く．
    const std::array<std::array<std::uint32_t, 2>, 8> points{{
        {0, 0}, {width - 1, 0}, {0, height - 1}, {width - 1, height - 1},
        {width / 2, height / 2}, {width / 3, height / 2},
        {width - width / 3 - 1, height / 2}, {width / 2, height / 3}
    }};
    const float infinity = std::numeric_limits<float>::infinity();
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const std::array<renderer::RGB32f, 8> samples{{
        {0.0f, 0.0f, 0.0f},
        {1.0f, 0.5f, 0.18f},
        {-1.0f, 0.0031308f, 0.0031309f},
        {2.0f, 0.75f, 0.002f},
        {0.25f, 0.5f, 0.75f},
        {nan, 0.2f, 1.0f},
        {0.2f, infinity, 0.4f},
        {(std::numeric_limits<float>::max)(), 2.0f, -1.0f}
    }};
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto offset = points[i][1] * row_stride + points[i][0] * sizeof(renderer::RGB32f);
        std::memcpy(bytes.data() + offset, &samples[i], sizeof(samples[i]));
    }
    return bytes;
}

void run_test()
{
    renderer::viewer::WindowOptions options;
    options.title = "CUDA/OpenGL surface test";
    options.visible = false;
    options.vsync = false;
    options.check_gl_errors = true;
    renderer::viewer::Window window(options);
    renderer::viewer::Workspace workspace;

    // 破棄順：presenter -> transform/input -> CUDA context -> OpenGL window．
    const auto device = display::device_for_current_gl_context();
    auto context = std::make_shared<cdw::RetainPrimaryContext>(device);
    const auto host_input = make_input();
    cdw::DeviceBuffer pixels(context->handle(), host_input.size());
    cdw::copy_host_to_device(pixels.view(), std::span<const std::byte>(host_input));
    const renderer::gpu::ConstRGB32fImageView input{
        std::as_const(pixels).view(), width, height, row_stride};
    processing::OutputTransform transform(context->handle(),
        processing::output_transform_module_path(u8"" RENDERER_MODULE_DIRECTORY));
    display::SurfacePresenter presenter(context->handle(), context, width, height);

    renderer::viewer::WorkspacePanels panels;
    panels.draw_render_view = [&]
    {
        if (presenter.has_image())
        {
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(presenter.texture_name())),
                ImVec2(340.0f, 260.0f), ImVec2(0, 0), ImVec2(1, 1));
        }
    };
    panels.draw_inspector = [] { ImGui::TextUnformatted("Surface reuse / output transform test"); };

    const std::array<float, 4> exposures{0.0f, 0.5f, -1.0f, 1.0f};
    std::size_t completed = 0;
    std::array<unsigned int, 2> texture_names{};
    bool submitted = false;
    unsigned int idle_frames = 0;
    processing::OutputTransformSettings settings;
    const auto started = std::chrono::steady_clock::now();

    while (idle_frames < 8)
    {
        if (window.should_close()) throw std::runtime_error("Surface test window closed early");
        if (std::chrono::steady_clock::now() - started > std::chrono::seconds(90))
        {
            const auto stats = presenter.statistics();
            throw std::runtime_error("Surface test timed out: completed=" + std::to_string(completed)
                + ", submitted=" + std::to_string(submitted)
                + ", productions=" + std::to_string(stats.productions)
                + ", publications=" + std::to_string(stats.publications)
                + ", retirement_fences=" + std::to_string(stats.retirement_fences));
        }

        if (presenter.poll())
        {
            if (!submitted || completed >= exposures.size())
                throw std::runtime_error("Unexpected surface publication");
            const auto texture = presenter.texture_name();
            if (completed < texture_names.size())
            {
                texture_names[completed] = texture;
                if (completed == 1 && texture_names[0] == texture_names[1])
                    throw std::runtime_error("Surface slots must use two distinct textures");
            }
            else if (texture != texture_names[completed % texture_names.size()])
                throw std::runtime_error("Surface presenter did not reuse the expected texture");
            display::tests::verify_surface(texture, input, transform, settings);
            ++completed;
            submitted = false;
        }

        if (!submitted && completed < exposures.size())
        {
            settings.exposure_ev = exposures[completed];
            submitted = presenter.try_submit([input, &transform, settings]
                (renderer::gpu::RGBA8SurfaceView surface, const cdw::Stream& stream)
            {
                transform.enqueue(input, surface, stream, settings);
            });
        }

        // 実際に GL から sample する．3 回目以降は退役フェンスを確認して texture を再利用する．
        if (!window.begin_frame()) continue;
        if (workspace.draw(panels, {"Checking CUDA/OpenGL surface output...", completed < exposures.size()}))
            window.request_close();
        window.end_frame();
        if (completed == exposures.size()) ++idle_frames;
    }

    const auto stats = presenter.statistics();
    if (presenter.pending() || stats.textures != 2 || stats.registrations != 2
        || stats.mappings != 4 || stats.surface_objects != 4 || stats.productions != 4
        || stats.publications != 4 || stats.retirement_fences != 3)
        throw std::runtime_error("Unexpected surface resource/production counts");
    std::cout << "CUDA/OpenGL surface: PASS (4 surface writes, 2 textures, 3 retirement fences, "
                 "8 idle UI frames)\n";
}
}

int main()
{
    try
    {
        run_test();
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "cuda_gl_surface_test: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
