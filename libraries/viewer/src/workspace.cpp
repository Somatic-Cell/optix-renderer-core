#include <renderer/viewer/workspace.hpp>

#include <imgui.h>

#include <imgui_internal.h>

#include <cfloat>
#include <cmath>

#ifndef IMGUI_HAS_DOCK
#error "The viewer requires the pinned Dear ImGui docking branch."
#endif

namespace renderer::viewer
{
namespace {
// パネル名の定義
constexpr const char* render_view_name = "Render view";
constexpr const char* inspector_name = "Inspector";
constexpr const char* activity_name = "Activity";

// 
class WindowScope final
{
public:
    explicit WindowScope(const char* name) : visible_(ImGui::Begin(name)) {}
    ~WindowScope() {ImGui::End();}
    // コピーの禁止
    WindowScope(const WindowScope&) = delete;
    WindowScope& operator=(const WindowScope) = delete;

    [[nodiscard]] bool visible() const noexcept { return visible_; }
private:
    bool visible_;
};

// 画面を 3 つの領域に分割し，それぞれに ImGui のウィンドウを配置
void build_layout(ImGuiID id, const ImVec2& size)
{
    // 
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiTabBarFlags_DockNode);
    ImGui::DockBuilderSetNodeSize(id, size);
 
    ImGuiID body = 0;
    ImGuiID activity = 0;
    ImGuiID inspector = 0;
    ImGuiID image = 0;

    // 領域を分割
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.18f, &activity, &body);        // 下側に 18 % activity, 上側に 82 % body を置く
    ImGui::DockBuilderSplitNode(body, ImGuiDir_Right, 0.27f, &inspector, &image);   // body の右側 27 % に inspector, 73 % に image を置く 
    
    // 各ノードにウィンドウを配置
    ImGui::DockBuilderDockWindow(render_view_name, image);
    ImGui::DockBuilderDockWindow(inspector_name, inspector);
    ImGui::DockBuilderDockWindow(activity_name, activity);
    
    ImGui::DockBuilderFinish(id);
}
} // namespace 

bool Workspace::draw(
    const WorkspacePanels& panels, 
    const ActivityStatus& activity
)
{
    bool close_required = false;
    
    // メニューバーを構築
    if (ImGui::BeginMainMenuBar())
    {
        if(ImGui::BeginMenu("File"))
        {
            if (panels.draw_file_menu)
            {
                panels.draw_file_menu();
                ImGui::Separator();
            }
            close_required = ImGui::MenuItem("Exit");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View"))
        {
            // 初期レイアウトを復元する
            if ( ImGui::MenuItem("Reset workspace")){
                reset_layout_ = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // DockSpace の構築
    const ImGuiID dockspace_id = ImHashStr("RendererWorkspace V1");
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    if(reset_layout_ || ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
    {
        build_layout(dockspace_id, viewport->WorkSize);
        reset_layout_ = false;
    }

    ImGui::DockSpaceOverViewport(dockspace_id, viewport);

    // Render view と Inspector を描画
    {
        const WindowScope panel(render_view_name);
        if (panel.visible() && panels.draw_render_view)
        {
            panels.draw_render_view();
        }
    }
    {
        const WindowScope panel(inspector_name);
        if (panel.visible() && panels.draw_inspector)
        {
            panels.draw_inspector();
        }
    }
    // Activity パネルの描画
    {
        const WindowScope panel(activity_name);
        if (panel.visible())
        {
            ImGui::TextUnformatted(activity.message.c_str());
            if (activity.busy)
            {
                const float phase = 1.0f + static_cast<float>(std::fmod(ImGui::GetTime(), 1024.0));
                ImGui::ProgressBar(-phase, ImVec2(-FLT_MIN, 0.0f), "");
            }
        }
    }
    return close_required; // ユーザがメニューから終了を要求したかどうか
}

} // namespace renderer::viewer