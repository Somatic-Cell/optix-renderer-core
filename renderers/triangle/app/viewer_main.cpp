#include <renderer/viewer/window.hpp>
#include <renderer/viewer/workspace.hpp>

#include <imgui.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
void run_viewer(bool smoke_test)
{
    namespace viewer = renderer::viewer;
    viewer::WindowOptions options;
    options.title = "Triangle viewer - V1 UI shell";
    options.visible = !smoke_test;
    options.vsync = !smoke_test;
    options.check_gl_errors = smoke_test;
    viewer::Window window(options);
    viewer::Workspace workspace;
    const auto& graphics = window.graphics_info();
    std::cout << "OpenGL: " << graphics.version << '\n'
              << "Device: " << graphics.renderer << '\n'
              << "V1: UI only; no OptiX job or model loader is connected.\n";

    bool preview_busy_indicator = false;
    bool vsync = options.vsync;
    viewer::WorkspacePanels panels;
    panels.draw_render_view = []
    {
        ImGui::TextUnformatted("No renderer frame is connected in V1.");
        ImGui::TextWrapped("This area is reserved for GPU-rendered images. "
            "No saved image, CPU readback, or substitute triangle is used.");
        const auto size = ImGui::GetContentRegionAvail();
        ImGui::Separator();
        ImGui::Text("Display area: %.0f x %.0f logical pixels", size.x, size.y);
    };
    panels.draw_inspector = [&]
    {
        ImGui::TextUnformatted("Triangle viewer / V1");
        ImGui::Separator();
        ImGui::TextWrapped("Only the window, docking, input and activity indicator "
            "are implemented in this stage.");
        ImGui::Spacing();
        if (ImGui::Checkbox("VSync", &vsync))
            window.set_vsync(vsync);
        ImGui::Checkbox("Preview busy indicator (UI only)", &preview_busy_indicator);
        ImGui::TextWrapped("Preview does not start a background task.");
        ImGui::Spacing();
        ImGui::Text("UI: %.1f FPS", ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextWrapped("%s", graphics.vendor.c_str());
        ImGui::TextWrapped("%s", graphics.renderer.c_str());
        ImGui::TextWrapped("OpenGL %s", graphics.version.c_str());
        ImGui::Text("GLFW + Dear ImGui %s", IMGUI_VERSION);
    };

    std::size_t frames = 0;
    const auto started = std::chrono::steady_clock::now();
    while (!window.should_close())
    {
        if (smoke_test && std::chrono::steady_clock::now() - started > std::chrono::seconds(10))
            throw std::runtime_error("Viewer smoke test timed out before completing frames");
        if (!window.begin_frame())
            continue;
        const bool busy = smoke_test ? frames < 6 : preview_busy_indicator;
        const viewer::ActivityStatus activity{
            busy ? "UI animation preview only; no renderer job is running."
                 : "Idle - V1 UI shell; renderer connection is scheduled for V2.",
            busy};
        if (workspace.draw(panels, activity))
            window.request_close();
        window.end_frame();
        ++frames;
        if (smoke_test)
        {
            if (frames == 4)
                window.resize(1000, 680);
            if (frames == 8)
                workspace.reset_layout();
            if (frames == 12)
                window.request_close();
        }
    }
    if (smoke_test && frames != 12)
        throw std::runtime_error("Viewer closed before the smoke test finished");
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        bool smoke_test = false;
        if (argc == 2 && std::string_view(argv[1]) == "--help")
        {
            std::cout << "Usage: triangle_viewer [--smoke-test]\n";
            return EXIT_SUCCESS;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--smoke-test")
            smoke_test = true;
        else if (argc != 1)
            throw std::invalid_argument("Usage: triangle_viewer [--smoke-test]");

        run_viewer(smoke_test);
        if (smoke_test)
        {
            // A second initialization exercises shutdown/recreation of owned UI resources.
            run_viewer(true);
            std::cout << "Viewer smoke: PASS (2 contexts, 24 frames)\n";
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    {
        std::cerr << "triangle_viewer: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
