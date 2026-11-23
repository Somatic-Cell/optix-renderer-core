#include <renderer/viewer/window.hpp>

#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>

#include "platform/window_theme.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <exception>
#include <stdexcept>
#include <thread>

namespace renderer::viewer
{

namespace 
{
// レンダラは複数スレッドで動かす．
// 複数のスレッドからアクセスしたときにアトミック操作する
std::atomic_bool window_active{false};

void report_glfw_error(int code, const char* message) noexcept
{
    std::fprintf(stderr, "[GLFW %d] %s\n", code, message ? message : "unknown error");
}

// GLFW の戻り値だけではエラー名の内容が分かりにくいので，その診断結果を例外メッセージに含める
std::string glfw_failure(const char* operation)
{
    const char* description = nullptr;
    const int code = glfwGetError(&description);
    return std::string(operation) + "failed (" + std::to_string(code) + "): "
        + (description ? description : "no GLFW diagnostic");
}

} // namespace

struct Window::Impl
{
    const std::thread::id owner_thread_ = std::this_thread::get_id();   // 現在のスレッドを識別する値を返す
    GLFWwindow*     window_             = nullptr;
    ImGuiContext*   imgui_context_      = nullptr;
    GLFWerrorfun    previous_error_callback_ = nullptr;
    
    // 初期化がどこまで成功したのかを記録するフラグ
    bool claimed_    = false;           // Window の利用権を取得できたかどうか
    bool callback_installed_ = false;   // 新しいコールバックを登録できたかどうか
    bool glfw_initialized_ = false;     // GLFW が初期化できたかどうか
    bool platform_initialized_ = false; // 
    bool renderer_initialized_ = false; // 
    bool frame_active_ = false;         // 
    bool check_gl_errors_ = false;      // OpenGL 周りの初期化が適切に行えたかどうか
    float dpi_scale_ = 0.0f;             // 
    ImGuiStyle base_style_{};
    detail::WindowTheme window_theme_;

    explicit Impl(const WindowOptions& options)
    {
        // 入力値の妥当性の検証
        if(options.width <= 0 || options.height <= 0 || options.title.empty() || options.title.find('\0') != std::string::npos)
        {
            throw std::invalid_argument("Window title and dimensions must be valid");
        }

        // Window の利用権を取得
        // window_active は atomic_bool
        // window_active の変更前の値を取得しつつ，true を代入する
        // Window は一つだけ起動するため， false であってほしい 
        // TODO: 本当にアトミック処理で良いのか？
        if(window_active.exchange(true))
        {
            throw std::logic_error("Only one viewer Window may be active");
        }

        // Window の利用権を取得できたことを記録
        claimed_ = true;

        try
        {
            // --------------
            // ImGui
            // --------------
            // 既存の ImGui コンテキストがないかどうか確認（共存させない）
            if(ImGui::GetCurrentContext() != nullptr)
            {
                throw std::logic_error("An ImGui context is already active");
            }

            // --------------
            // GLFW
            // --------------
            // 元のコールバックを保存し，新しいコールバックを登録
            previous_error_callback_ = glfwSetErrorCallback(report_glfw_error);
            callback_installed_ = true;

            // GLFW を初期化
            if (!glfwInit())
            {
                throw std::runtime_error(glfw_failure("glfwInit"));
            }

            glfw_initialized_ = true;

            // Window 生成時の条件を指定

            // 生成の条件をデフォルトの値に戻す
            glfwDefaultWindowHints();
            // OpenGL 4.5 を要求
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
            
            // 
            glfwWindowHint(
                GLFW_OPENGL_PROFILE, 
                GLFW_OPENGL_CORE_PROFILE    // プログラマブルパイプラインを使って描画するため
            );
            
            // Window を非表示にするかどうか
            glfwWindowHint(
                GLFW_VISIBLE, 
                options.visible ? GLFW_TRUE : GLFW_FALSE
            );

            // フレームbufferで MSAA を要求しない
            glfwWindowHint(GLFW_SAMPLES, 0);

            // window の作成
            window_ = glfwCreateWindow(
                options.width, 
                options.height, 
                options.title.c_str(), 
                nullptr,                // フルスクリーン表示に使用するモニタ (nullptr: 通常の window)
                nullptr                 // 依存する window
            );

            if(!window_)
            {
                throw std::runtime_error(glfw_failure("glfwCraeteWindow"));
            }

            // テーマの設定
            window_theme_.update(window_);

            
            // OpenGL の描画状態，オブジェクトを扱うための実行環境 (コンテキスト) を作成
            glfwMakeContextCurrent(window_);

            // 
            glfwSetWindowSizeLimits(
                window_, 
                640, 420,                       // 最小サイズ
                GLFW_DONT_CARE, GLFW_DONT_CARE  // 最大サイズ (指定しない)
            );

            glfwSwapInterval(options.vsync ? 1 : 0);
            
            check_gl_errors_ = options.check_gl_errors;

            // --------------
            // ImGui のコンテキスト生成
            // --------------
            IMGUI_CHECKVERSION();
            imgui_context_ = ImGui::CreateContext();
            
            // ImGui の入出力の設定
            ImGuiIO& io = ImGui::GetIO();

            // 
            io.ConfigFlags |= 
                ImGuiConfigFlags_NavEnableKeyboard  // キーボードを使った UI 操作 
                | ImGuiConfigFlags_DockingEnable;   // ImGui ウィンドウのドッキング
            
            // マルチビューポートは無効化
            // GLFW を使った一つの OS ウィンドウ内で UI を管理する
            io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

            // レイアウトの保存，再利用は行わない
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            
            io.ConfigWindowsMoveFromTitleBarOnly = true;
            
            ImGui::StyleColorsDark();
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg].w = 1.0f;   // 背景は不透明
            base_style_ = ImGui::GetStyle();                        // 一旦 base_style_ に設定を退避
            update_dpi();

            // GLFW を初期化
            // ImGui と GLFW を連携 (GLFW が ImGUI にイベントを伝える)
            if(!ImGui_ImplGlfw_InitForOpenGL(window_, true))
            {
                throw std::runtime_error("ImGui GLFW backend initialization failed");
            }
            platform_initialized_ = true;

            // OpenGL を初期化
            // ImGui の描画結果を OpenGL で描画するための初期化
            if (!ImGui_ImplOpenGL3_Init("#version 450 core"))
            {
                throw std::runtime_error("ImGui OpenGL backend initialization failed");
            }
            renderer_initialized_ = true;

            // OpenGL の描画に必要な GPU オブジェクトを生成
            if(!ImGui_ImplOpenGL3_CreateDeviceObjects())
            {
                throw std::runtime_error("ImGui OpenGL device-object creation failed");
            }
        }
        catch(...)
        {
            // コンストラクタが例外を投げて失敗した場合，デストラクタが呼ばれないので明示的に開放
            cleanup();
            throw;
        }
        
    }

    ~Impl() noexcept
    {
        // デストラクタはインストラクタを呼んだスレッドと同じスレッド（メインスレッド）で実行する
        if(std::this_thread::get_id() != owner_thread_)
        {
            std::fputs("Viewer Window must be destroyed on its creating main thread\n", stderr);
            std::terminate();
        }
        cleanup();
    }

    // Window を構築したスレッド (メインスレッド) であることを要求する
    void require_owner() const 
    {
        if(std::this_thread::get_id() != owner_thread_)
        {
            throw std::logic_error("Viewer Window is main-thread only");
        }
    }

    
    void activate() const
    {
        require_owner();
        if(glfwGetCurrentContext() != window_)
        {
            glfwMakeContextCurrent(window_);
        }
        ImGui::SetCurrentContext(imgui_context_);
    }

    void update_dpi()
    {
        // スケールの取得
        float x = 1.0f;
        float y = 1.0f;

        glfwGetWindowContentScale(window_, &x, &y);
        float scale = (std::max)(x, y);

        // スケールの有効性チェック
        if (!std::isfinite(scale) || scale <= 0.0f)
        {
            scale = 1.0f;
        }
        // 変化が微小なら無視
        if (std::abs(scale - dpi_scale_) < 0.001f)
        {
            return;
        }

        dpi_scale_ = scale;
        auto & style = ImGui::GetStyle();

        // 毎回 base_style_ で初期化し直すことで，誤差の累積を防ぐ
        style = base_style_;
        style.ScaleAllSizes(scale);
        style.FontScaleDpi = scale;
    }

    // 初期化済みのリソースを初期化とは逆順に開放する
    void cleanup() noexcept
    {

        if (window_)
        {
            glfwMakeContextCurrent(window_);
        }

        if(imgui_context_)
        {
            ImGui::SetCurrentContext(imgui_context_);
            if(frame_active_)
            {
                ImGui::EndFrame();
                frame_active_ = false;
            }
            if (renderer_initialized_)
            {
                ImGui_ImplOpenGL3_Shutdown();
            }

            if (platform_initialized_)
            {
                ImGui_ImplGlfw_Shutdown();
            }

            ImGui::DestroyContext(imgui_context_);

            imgui_context_ = nullptr;
        }

        renderer_initialized_ = false;
        platform_initialized_ = false;

        if (window_)
        {
            glfwDestroyWindow(window_);
            window_ = nullptr;
        }

        if (glfw_initialized_)
        {
            glfwTerminate();
            glfw_initialized_ = false;
        }

        if (claimed_)
        {
            if(callback_installed_)
            {
                glfwSetErrorCallback(previous_error_callback_);
                callback_installed_ = false;
            }
            window_active.store(false);
            claimed_ = false;
        }
        
    }

};

Window::Window(const WindowOptions& options) : impl_(std::make_unique<Impl>(options)) {}
Window::~Window() noexcept = default;

bool Window::should_close() const
{
    impl_->require_owner();
    return glfwWindowShouldClose(impl_->window_) != 0;
}

void Window::request_close()
{
    impl_->require_owner();
    glfwSetWindowShouldClose(impl_->window_, GLFW_TRUE);
}

bool Window::begin_frame()
{
    impl_->activate();
    if (impl_->frame_active_)
    {
        throw std::logic_error("end_frame() is required before starting another frame");
    }

    glfwPollEvents();
    if(should_close())
    {
        return false;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->window_, &width, &height);
    if ( glfwGetWindowAttrib(impl_->window_, GLFW_ICONIFIED) || width <= 0 || height <= 0)
    {
        glfwWaitEventsTimeout(0.05);
        return false;
    }

    impl_->update_dpi();
    impl_->window_theme_.update(impl_->window_);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    impl_->frame_active_ = true;
    return true;
}

void Window::end_frame()
{
    impl_->activate();
    if (!impl_->frame_active_)
    {
        throw std::logic_error("begin_frame() must succeed before end_frame()");
    }

    ImGui::Render();
    
    impl_->frame_active_ = false;
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(impl_->window_, &width, &height);

    if( width > 0 && height > 0)
    {
        glViewport(0, 0, width, height);
        glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        if (impl_->check_gl_errors_)
        {
            const GLenum error = glGetError();
            if(error != GL_NO_ERROR)
            {
                throw std::runtime_error("OpenGL error: " + std::to_string(error));
            }
        }
        glfwSwapBuffers(impl_->window_);
    }
}

void Window::set_vsync(bool enabled)
{
    impl_->activate();
    glfwSwapInterval(enabled ? 1 : 0);
}

void Window::resize(int width, int height)
{
    impl_->require_owner();
    if (width <= 0 || height <= 0)
    {
        throw std::invalid_argument("Window dimensions must be positive");
    }
    glfwSetWindowSize(impl_->window_, width, height);
}

} // namespace renderer::viewer 