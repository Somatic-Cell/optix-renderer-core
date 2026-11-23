#pragma once

#include <chrono>

struct GLFWwindow;

namespace renderer::viewer_ui::detail
{

    class WindowTheme final
{
public:
    void update(GLFWwindow* window) noexcept;
private:
    std::chrono::steady_clock::time_point last_check_{};
    bool checked_ = false;
    bool initialized_ = false;
    bool high_contrast_ = false;
    bool warned_ = false;
};

} // namespace renderer::viewer_ui::detail