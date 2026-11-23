#include <renderer/app_support/cli.hpp>
#include <renderer/triangle/viewer_application.hpp>

#include <filesystem>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must point to staged GPU modules
#endif

int main(int argc, char** argv)
{
    return renderer::app_support::run_viewer_cli(argc, argv, "triangle_viewer", u8"" RENDERER_MODULE_DIRECTORY,
        [](const std::filesystem::path& module_directory)
        {
            return renderer::render_viewer::run(renderer::triangle::make_viewer_application(module_directory));
        });
}
