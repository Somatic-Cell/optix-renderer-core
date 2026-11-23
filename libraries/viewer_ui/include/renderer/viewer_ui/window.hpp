#pragma once

#include <memory>
#include <string>

namespace renderer::viewer_ui
{

struct WindowOptions
{
    std::string title = "Renderer viewer";  // アプリ名
    // 16:9 HD
    int width = 1440;
    int height = 900;
    bool visible = true;
    bool vsync = true;
    bool check_gl_errors = false;
};

// GLFW のウィンドウはメインスレッドで生成し，同じスレッドで操作・破棄する
class Window final{
public:
    explicit Window(const WindowOptions& options = {});
    ~Window() noexcept;

    // copy / move の禁止
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&&) = delete;
    Window& operator=(Window&&) = delete;
    
    // ウィンドウを持つ上位の UI も，自分の状態に触れる前に同じスレッドの制約を検査できる
    void require_owner_thread() const;
    [[nodiscard]] bool should_close() const;
    void request_close();

    // イベントを監視する
    [[nodiscard]] bool begin_frame();
    void end_frame();

    void set_vsync(bool enabled);
    void resize(int width, int height);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace renderer::viewer_ui