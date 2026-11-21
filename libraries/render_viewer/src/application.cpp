#include <renderer/render_viewer/application.hpp>
#include <renderer/render_viewer/viewer.hpp>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace renderer::render_viewer
{


int run(Application app, int argc, char** argv)
{
    try
    {
        for (int i = 1; i < argc; ++i)
        {
            const std::string_view option(argv[i]);
            if (option == "--help")
            {
                std::cout << "Usage: viewer [--modules DIRECTORY]\n";
                return EXIT_SUCCESS;
            }
            else if (option == "--modules" && i + 1 < argc) modules = argv[++i];
            else throw std::invalid_argument("Unknown or incomplete option; use --help");
        }

        Viewer viewer(std::move(app));
        while (viewer.update()) {}
        const auto status = viewer.status();

        if (status.error)
        {
            std::rethrow_exception(status.error);
        }
        return EXIT_SUCCESS;
    }
    catch (const std::exception& error)
    { 
        std::cerr << "viewer: " << error.what() << '\n'; 
        return EXIT_FAILURE; 
    }
    catch (...)
    { 
        std::cerr << "viewer: unknown failure\n"; 
        return EXIT_FAILURE; 
    }
}
} // namespace renderer::render_viewer
