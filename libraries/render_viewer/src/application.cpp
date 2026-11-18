#include <renderer/render_viewer/application.hpp>
#include <renderer/cuda_gl_display/sufrece_presenter.hpp>
#include <renderer/image_processing/output_transform.hpp>
#include <renderer/task_execution/session_worker.hpp>
#include <renderer/viewer/window.hpp>
#include <renderer/viewer/workspace.hpp>
#include <cuda_driver_wrapper/event.hpp>
#include <imgui.h>
#ifdef RENDERER_VIEWER_TESTS
#include "ui_smoke.hpp"
#endif
#ifdef RENDERER_VIEWER_GPU_CHECKS
#include "verify_surface.hpp"
#endif
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace renderer::render_viewer
{
namespace
{
namespace tasks = task_execution;
namespace cdw = cuda_driver_wrapper;
struct DisplayProgram
{
    std::shared_ptr<const void> context_owner;
    image_processing::OutputTransform transform;
    DisplayProgram(SourceInfo info, const std::filesystem::path& path)
        : context_owner(std::move(info.context_lifetime)), transform(info.context, path) {}
};
struct Backend
{
    std::unique_ptr<FrameSource> source; // persistent renderer, destroyed on worker
    std::shared_ptr<DisplayProgram> display;
};
struct Reply
{
    std::optional<gpu::SubmittedRGB32fImage> image;
    std::shared_ptr<DisplayProgram> display;
};
using Worker = tasks::SessionWorker<Backend, Reply>;
std::string failure_text(std::exception_ptr error)
{
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception& e) { return e.what(); }
    catch (...) { return "Unknown worker failure"; }
    return {};
}

void gpu_viewer(Application app, const std::filesystem::path& modules, bool smoke)
{
    if (!app.create_source) throw std::invalid_argument("A frame-source factory is required");
    image_processing::validate_output_transform_settings(app.display);
    viewer::WindowOptions options;
    options.title = app.title;
    options.visible = !smoke;
    options.vsync = !smoke;
    options.check_gl_errors = smoke;
    viewer::Window window(options);
    viewer::Workspace workspace;
    // Reverse teardown: presenter -> image/module leases -> worker -> window.
    std::unique_ptr<Worker> worker;
    tasks::WorkerSnapshot<Reply> snapshot;
    std::optional<gpu::SubmittedRGB32fImage> latest;
    std::optional<gpu::SubmittedRGB32fImage> producing;
    std::shared_ptr<DisplayProgram> display;
    std::unique_ptr<cuda_gl_display::SurfacePresenter> presenter;
    std::string error;
    bool vsync = options.vsync, want_render = true;
    bool exposure_test_started = false;
    auto display_settings = app.display;
    auto producing_settings = display_settings;
    std::uint64_t revision = 1, sent_revision = 0, sent_sequence = 0, seen_generation = 0;
    unsigned int received = 0, publications = 0, final_frames = 0;
    std::size_t ui_frames = 0;
    const auto started = std::chrono::steady_clock::now();

    viewer::WorkspacePanels panels;
    panels.draw_render_view = [&]
    {
        if (!error.empty()) { ImGui::TextWrapped("%s", error.c_str()); return; }
        if (!presenter || !presenter->has_image())
        { ImGui::TextUnformatted("Preparing the first GPU image..."); return; }
        const auto available = ImGui::GetContentRegionAvail();
        if (available.x <= 0 || available.y <= 0) return;
        const float w = static_cast<float>(presenter->width());
        const float h = static_cast<float>(presenter->height());
        const float scale = (std::min)(available.x / w, available.y / h);
        const ImVec2 size(w * scale, h * scale);
        const auto p = ImGui::GetCursorPos();
        ImGui::SetCursorPos(ImVec2(p.x + (available.x - size.x) * 0.5f,
            p.y + (available.y - size.y) * 0.5f));
        // Row 0 is TOP for the renderer AND this surface. No framebuffer Y flip.
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(presenter->texture_name())),
            size, ImVec2(0, 0), ImVec2(1, 1));
    };
    panels.draw_inspector = [&]
    {
        ImGui::TextUnformatted("Persistent session / surface display");
        ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Text("Render submissions received: %u", received);
        ImGui::Text("Display updates: %u", publications);
        if (ImGui::Checkbox("VSync", &vsync)) window.set_vsync(vsync);
        const bool ready = worker && snapshot.accepting() && error.empty();
        ImGui::BeginDisabled(!ready || want_render);
        if (ImGui::Button("Render again")) want_render = true;
        ImGui::EndDisabled();
        if (ImGui::SliderFloat("Exposure (EV)", &display_settings.exposure_ev,
            image_processing::minimum_exposure_ev, image_processing::maximum_exposure_ev))
            ++revision;
        ImGui::TextWrapped("Exposure only updates the display surface. Resizing the UI does not render or rebuild the scene.");
        if (!error.empty()) ImGui::TextWrapped("%s", error.c_str());
        if (app.draw_inspector) { ImGui::Separator(); app.draw_inspector(); }
    };

    while (!window.should_close())
    {
        if (smoke && std::chrono::steady_clock::now() - started > std::chrono::seconds(120))
            throw std::runtime_error("Surface viewer smoke timed out");
        if (worker)
        {
            snapshot = worker->snapshot();
            if (snapshot.error)
            {
                error = failure_text(snapshot.error);
                if (smoke) std::rethrow_exception(snapshot.error);
            }
            if (snapshot.result && snapshot.generation != seen_generation)
            {
                seen_generation = snapshot.generation;
                if (snapshot.result->image)
                {
                    latest = snapshot.result->image;
                    display = snapshot.result->display;
                    ++received;
                    if (!presenter)
                    {
                        const auto view = latest->image();
                        presenter = std::make_unique<cuda_gl_display::SurfacePresenter>(
                            view.memory.context, latest->context_lifetime(), view.width, view.height);
                    }
                }
                else want_render = true; // backpressure, keep the request
            }
        }
        if (presenter && error.empty())
        {
            try
            {
                if (presenter->poll())
                {
                    ++publications;
#ifdef RENDERER_VIEWER_GPU_CHECKS
                    if (smoke)
                        tests::verify_surface(presenter->texture_name(), *producing,
                            display->transform, producing_settings);
#endif
                    producing.reset();
                }
                if (latest && !presenter->pending()
                    && (sent_sequence != latest->sequence() || sent_revision != revision))
                {
                    const auto frame = *latest;
                    const auto settings = display_settings;
                    // Retain event+pixels+context and module until surface completion.
                    const bool accepted = presenter->try_submit([frame, program = display, settings]
                        (gpu::RGBA8SurfaceView surface, const cdw::Stream& stream)
                    {
                        // This is a GPU dependency, NOT a CPU wait for rendering.
                        cdw::stream_wait_event(stream, frame.completion_event());
                        program->transform.enqueue(frame.image(), surface, stream, settings);
                    });
                    if (accepted)
                    {
                        producing = frame;
                        producing_settings = settings;
                        sent_sequence = frame.sequence();
                        sent_revision = revision;
                    }
                }
            }
            catch (...)
            {
                if (smoke) throw;
                error = failure_text(std::current_exception());
            }
        }
        if (!window.begin_frame()) continue;
        viewer::ActivityStatus activity;
        if (!error.empty()) activity = {error, false};
        else if (!worker) activity = {"Connecting the display GPU...", true};
        else if (presenter && presenter->pending()) activity = {"Rendering / updating surface...", true};
        else if (!snapshot.accepting()) activity = {snapshot.message, true};
        else activity = {"Idle: no renderer/transfer work is issued for UI-only refreshes.", false};
        if (workspace.draw(panels, activity)) window.request_close();
        window.end_frame();
        ++ui_frames;
        if (!worker && !window.should_close())
        {
            // Visible UI exists before OptiX/module/GAS preparation starts.
            const LaunchEnvironment environment{cuda_gl_display::device_for_current_gl_context(), modules, smoke};
            auto factory = app.create_source;
            worker = std::make_unique<Worker>([factory, environment](const tasks::TaskContext& task)
            {
                auto backend = std::make_unique<Backend>();
                task.report("Preparing renderer session...");
                backend->source = factory(environment, task);
                if (!backend->source) throw std::runtime_error("Frame source is null");
                task.report("Loading the shared linear/surface transform module...");
                backend->display = std::make_shared<DisplayProgram>(backend->source->info(),
                    environment.module_directory / "image_processing" / "output_transform.fatbin");
                return backend;
            }, [](Backend& backend) { return backend.source->correct(); });
        }
        if (smoke && error.empty() && presenter && !presenter->pending())
        {
            if (publications == received && received < 3) want_render = true;
            if (received == 3 && publications == 3 && !exposure_test_started)
            {
                display_settings.exposure_ev = 0.5f;
                ++revision;
                exposure_test_started = true;
            }
            if (publications == 4 && ++final_frames >= 8) window.request_close();
        }
        if (worker && want_render && error.empty() && !window.should_close())
        {
            if (worker->try_submit([](Backend& backend, const tasks::TaskContext& task)
            {
                task.report("Submitting a frame to the existing scene...");
                return Reply{backend.source->try_render(), backend.display};
            })) want_render = false;
        }
    }
    if (smoke)
    {
        if (!presenter || received != 3 || publications != 4 || final_frames < 8)
            throw std::runtime_error("Surface smoke closed before repeated rendering/redisplay finished");
        const auto stats = presenter->statistics();
        if (stats.textures != 2 || stats.registrations != 2 || stats.productions != 4
            || stats.publications != 4 || stats.retirement_fences != 3)
            throw std::runtime_error("Unexpected presentation resource/production counts");
        std::cout << "Surface viewer: PASS (3 render submissions, 4 surface writes, 2 textures, "
                  << ui_frames << " UI frames; no PBO/readback in normal display)\n";
    }
    // Release display consumers before session teardown; heavy renderer teardown
    // happens on worker. Presenter cleanup may block only here/exception unwinding.
    presenter.reset();
    producing.reset();
    latest.reset();
    display.reset();
    snapshot = {};
    worker.reset();
}
}
int run(Application app, int argc, char** argv)
{
    try
    {
        bool ui_smoke = false, gpu_smoke = false;
        auto modules = app.module_directory;
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view option(argv[i]);
            if (option == "--help")
            {
                std::cout << "Usage: viewer [--modules DIRECTORY] [--smoke-test | --gpu-smoke-test]\n";
                return EXIT_SUCCESS;
            }
            if (option == "--smoke-test") ui_smoke = true;
            else if (option == "--gpu-smoke-test") gpu_smoke = true;
            else if (option == "--modules" && i + 1 < argc) modules = argv[++i];
            else throw std::invalid_argument("Unknown or incomplete option; use --help");
        }
        if (ui_smoke && gpu_smoke) throw std::invalid_argument("Select only one smoke mode");
        if (ui_smoke)
        {
#ifdef RENDERER_VIEWER_TESTS
            tests::run_ui_smoke();
#else
            throw std::invalid_argument("UI smoke requires BUILD_TESTING");
#endif
        }
        else
        {
#ifndef RENDERER_VIEWER_GPU_CHECKS
            if (gpu_smoke) throw std::invalid_argument("GPU smoke requires enabled GPU tests");
#endif
            gpu_viewer(std::move(app), modules, gpu_smoke);
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    { std::cerr << "viewer: " << error.what() << '\n'; return EXIT_FAILURE; }
}
}
