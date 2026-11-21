#include <renderer/render_session.hpp>
#include <renderer/render_viewer/application.hpp>
#include <memory>
#include <stdexcept>
#include <utility>
#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must point to staged GPU modules
#endif

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
    std::optional<renderer::gpu::SubmittedRGB32fImage> try_render() override
    {
        if (!session_.try_advance()) return std::nullopt;
        auto frame = session_.take_latest_image();
        if (!frame) throw std::logic_error("Submitted triangle render has no image");
        return frame;
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
int main(int argc, char** argv)
{
    renderer::render_viewer::Application application;
    application.title = "Triangle viewer - persistent session / surface";
    application.module_directory = RENDERER_MODULE_DIRECTORY;
    application.create_source = [](const auto& environment, const auto& task)
        -> std::unique_ptr<renderer::render_viewer::FrameSource>
    { return std::make_unique<TriangleSource>(environment, task); };
    return renderer::render_viewer::run(std::move(application), argc, argv);
}
