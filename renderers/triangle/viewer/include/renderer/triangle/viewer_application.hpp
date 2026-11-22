#pragma once

#include <renderer/render_viewer/application.hpp>
#include <renderer/render_viewer/session_frame_source.hpp>
#include <renderer/triangle/render_session.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>

namespace renderer::triangle
{

[[nodiscard]] inline render_viewer::Application make_viewer_application(
    const std::filesystem::path& module_directory)
{
    render_viewer::Application application;
    application.title = "Triangle viewer - persistent session / surface";
    application.module_directory = module_directory;
    application.create_source = [](const render_viewer::LaunchEnvironment& environment,
                                   const task_execution::TaskContext& task)
    {
        SessionOptions options;
        options.optix_validation = environment.validation;
        return std::make_unique<render_viewer::SessionFrameSource<RenderSession>>(
            environment.device, module_path(environment.module_directory), options,
            [&task](std::string message) { task.report(std::move(message)); });
    };
    return application;
}

} // namespace renderer::triangle
