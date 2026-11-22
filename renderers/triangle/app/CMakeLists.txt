renderer_add_application(triangle_cli
    SOURCES main.cpp run.cpp triangle_check.cpp
    LINK
        renderer::triangle
        renderer::image_processing
        renderer::image_export
        renderer::app_support
        renderer::triangle_config
        renderer::gpu_app_support
)

if(RENDERER_BUILD_VIEWER)
    include("${CMAKE_CURRENT_LIST_DIR}/viewer.cmake")
endif()
