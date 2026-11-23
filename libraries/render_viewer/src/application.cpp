#include <renderer/render_viewer/application.hpp>
#include <renderer/render_viewer/viewer.hpp>

#include <cstdlib>
#include <exception>
#include <utility>

namespace renderer::render_viewer
{


int run(Application app)
{
    Viewer viewer(std::move(app));
    while (viewer.update()) {}
    const auto status = viewer.status();
    if(status.error)
    {
        std::rethrow_exception(status.error);
    }
    return EXIT_SUCCESS;
}

} // namespace renderer::render_viewer
