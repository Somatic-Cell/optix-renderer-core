#include "viewer_application.hpp"

#include <utility>

#ifndef RENDERER_MODULE_DIRECTORY
#error RENDERER_MODULE_DIRECTORY must point to staged GPU modules
#endif

int main(int argc, char** argv)
{
    auto application = renderer::triangle::make_viewer_application(RENDERER_MODULE_DIRECTORY);
    return renderer::render_viewer::run(std::move(application), argc, argv);
}
