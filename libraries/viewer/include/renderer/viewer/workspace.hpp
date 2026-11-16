#pragma once

#include <functional>
#include <string>

namespace renderer::viewer
{

struct ActivityStatus
{
    std::string message = "Idle";
    bool busy = false;
};

struct WorkspacePanels
{
    std::function<void()>draw_render_view;
    std::function<void()>draw_inspector;
};

class Workspace final
{
public:
    [[nodiscard]] bool draw(const WorkspacePanels& panels, const ActivityStatus& activity);
    void reset_layout() noexcept { reset_layout_ = true; };
private:
    bool reset_layout_ = false;
};

} // namespace renderer::viewer