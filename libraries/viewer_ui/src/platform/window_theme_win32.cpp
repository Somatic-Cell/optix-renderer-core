#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <dwmapi.h>
#include "window_theme.hpp"
#include <cstdio>

namespace renderer::viewer_ui::detail
{
void WindowTheme::update(GLFWwindow* window) noexcept
{
    const auto now = std::chrono::steady_clock::now();
    if (checked_ && now - last_check_ < std::chrono::seconds(1)) return;
    
    last_check_ = now;
    checked_ = true;
    
    HIGHCONTRASTW contrast{};
    contrast.cbSize = static_cast<UINT>(sizeof(contrast));
    
    if (!SystemParametersInfoW(SPI_GETHIGHCONTRAST, contrast.cbSize, &contrast, 0)) return;
    
    const bool high_contrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
    
    if (initialized_ && high_contrast == high_contrast_) return;
    
    initialized_ = true;
    high_contrast_ = high_contrast;
    
    const HWND handle = glfwGetWin32Window(window);
    if (!handle) return;
    
    const auto set = [&](DWMWINDOWATTRIBUTE attribute, const auto& value)
    
    {
        const HRESULT result = DwmSetWindowAttribute(handle, static_cast<DWORD>(attribute),
            &value, static_cast<DWORD>(sizeof(value)));
        if (FAILED(result) && !warned_)
        {
            std::fprintf(stderr, "[viewer theme] DWM attribute failed (0x%08lX); retaining supported OS defaults\n",
                static_cast<unsigned long>(result));
            warned_ = true;
        }
    };
    
    const BOOL dark = high_contrast ? FALSE : TRUE;
    const COLORREF caption = high_contrast ? DWMWA_COLOR_DEFAULT : RGB(24, 24, 24);
    const COLORREF text = high_contrast ? DWMWA_COLOR_DEFAULT : RGB(235, 235, 235);
    const COLORREF border = high_contrast ? DWMWA_COLOR_DEFAULT : RGB(48, 48, 48);
    
    set(DWMWA_USE_IMMERSIVE_DARK_MODE, dark);
    set(DWMWA_CAPTION_COLOR, caption);
    set(DWMWA_TEXT_COLOR, text);
    set(DWMWA_BORDER_COLOR, border);
}
} // namespace renderer::viewer_ui::detail
