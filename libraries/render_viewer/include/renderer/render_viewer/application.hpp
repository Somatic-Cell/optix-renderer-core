#pragma once

#include <renderer/gpu/submitted_image.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>
#include <renderer/task_execution/maintenance_status.hpp>
#include <renderer/task_execution/task_context.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace renderer::render_viewer
{
struct SourceInfo
{
    CUcontext context = nullptr;
    std::shared_ptr<const void> context_lifetime;
};

// ビューワのワーカースレッドが生成，操作，破棄する．取得した画像ハンドルは表示側に渡せる
class FrameSource
{
public:
    virtual ~FrameSource() noexcept = default;
    [[nodiscard]] virtual SourceInfo info() const = 0;
    // 1 回の描画を投入したら true, 空きがなく体裁思考が必要なら false
    // GPU の完了は待たない
    [[nodiscard]] virtual bool try_advance() = 0;
    // 細心の未取得の画像を一度だけ渡す．未取得の画像がなければ空のまま返す
    [[nodiscard]] virtual std::optional<gpu::SubmittedRGB32fImage> take_latest_image() = 0;
    virtual task_execution::MaintenanceStatus collect() = 0;
};

struct LaunchEnvironment
{
    CUdevice device = 0;
    std::filesystem::path module_directory;
    bool validation = false;
};

struct Application
{
    std::string title = "Renderer viewer";
    std::filesystem::path module_directory;
    image_processing::OutputTransformSettings display{};
    
    std::function<
        std::unique_ptr<FrameSource>(
            const LaunchEnvironment&,
            const task_execution::TaskContext&
        )
    > create_source;

    std::function<void()> draw_inspector;
};

// ウインドウが閉じるまで更新する．実行中のエラーは呼び出し側に伝える
int run(Application Application);
}