#include "viewer_application.hpp"

#include <renderer/triangle/render_session.hpp>

#include <memory>
#include <utility>

namespace
{
// Only this adapter knows the concrete renderer. No duplicated worker/display
// loop, GL resource management, transfer code or cancellation infrastructure.
class TriangleSource final : public renderer::render_viewer::FrameSource
{
public:
    TriangleSource(const renderer::render_viewer::LaunchEnvironment& environment,
        const renderer::task_execution::TaskContext& task)
        : session_(environment.device,
            environment.module_directory / "triangle" / "programs.optixir",
            options(environment.validation), [&](std::string text) { task.report(std::move(text)); }) {}
    renderer::render_viewer::SourceInfo info() const override
    {
        const auto image = session_.settings();
        return {session_.context(), session_.context_lifetime(), image.width, image.height};
    }
    bool try_advance() override { return session_.try_advance(); }
    std::optional<renderer::gpu::SubmittedRGB32fImage> take_latest_image() override
    {
        return session_.take_latest_image();
    }
    bool collect() override { return session_.collect(); }
private:
    static renderer::triangle::SessionOptions options(bool validation)
    {
        renderer::triangle::SessionOptions result;
        result.optix_validation = validation;
        return result;
    }
    renderer::triangle::RenderSession session_;
};
}

namespace renderer::triangle
{

render_viewer::Application make_viewer_application(const std::filesystem::path& module_directory)
{
    render_viewer::Application application;
    application.title = "Triangle viewer - persistent session / surface";
    application.module_directory = module_directory;
    application.create_source = [](const auto& environment, const auto& task)
        -> std::unique_ptr<render_viewer::FrameSource>
    { return std::make_unique<TriangleSource>(environment, task); };
    return application;
}

} // namespace renderer::triangle
