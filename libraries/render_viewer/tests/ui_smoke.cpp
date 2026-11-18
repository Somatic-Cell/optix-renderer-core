#include "ui_smoke.hpp"
#include <renderer/viewer/window.hpp>
#include <renderer/viewer/workspace.hpp>
#include <imgui.h>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace renderer::render_viewer::tests
{
void run_ui_smoke()
{
    for (int cycle = 0; cycle != 2; ++cycle)
    {
        viewer::WindowOptions options;
        options.title = "Viewer UI smoke";
        options.visible = false;
        options.vsync = false;
        options.check_gl_errors = true;
        viewer::Window window(options);
        viewer::Workspace workspace;
        viewer::WorkspacePanels panels;
        panels.draw_render_view = [] { ImGui::TextUnformatted("UI test; no renderer is connected."); };
        panels.draw_inspector = [] { ImGui::TextUnformatted("GLFW / ImGui lifecycle test"); };
        int frames = 0;
        const auto started = std::chrono::steady_clock::now();
        while (!window.should_close())
        {
            if (std::chrono::steady_clock::now() - started > std::chrono::seconds(10))
                throw std::runtime_error("UI smoke timed out");
            if (!window.begin_frame()) continue;
            if (workspace.draw(panels, {"UI animation test only", frames < 6})) window.request_close();
            window.end_frame();
            ++frames;
            if (frames == 4) window.resize(1000, 680);
            if (frames == 8) workspace.reset_layout();
            if (frames == 12) window.request_close();
        }
        if (frames != 12) throw std::runtime_error("UI smoke closed early");
    }
    std::cout << "Viewer smoke: PASS (2 contexts, 24 frames)\n";
}
}
