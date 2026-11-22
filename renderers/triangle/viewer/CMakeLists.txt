add_library(renderer_triangle_viewer_application STATIC src/viewer_application.cpp)
add_library(renderer::triangle_viewer_application ALIAS renderer_triangle_viewer_application)
renderer_configure_host(renderer_triangle_viewer_application)
target_include_directories(renderer_triangle_viewer_application
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_triangle_viewer_application
    PUBLIC renderer::render_viewer
    PRIVATE renderer::triangle
)
