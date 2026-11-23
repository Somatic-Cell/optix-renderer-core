#pragma once

#include <renderer/gpu/submitted_image.hpp>
#include <renderer/image_processing/output_transform_settings.hpp>
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

class FrameSource
{
public:
    virtual ~FrameSource() noexcept = default;
    [[nodiscard]] virtual SourceInfo info() const = 0;
    [[nodiscard]] virtual bool try_advance() = 0;
    [[nodiscard]] virtual std::optional<gpu::SubmittedRGB32fImage> take_latest_image() = 0;
    virtual bool collect() = 0;
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

int run(Application application, int argc, char** argv);
}