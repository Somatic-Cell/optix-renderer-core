add_library(renderer_triangle_viewer_application INTERFACE)
add_library(renderer::triangle_viewer_application ALIAS renderer_triangle_viewer_application)
target_include_directories(renderer_triangle_viewer_application
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(renderer_triangle_viewer_application
    INTERFACE renderer::render_viewer renderer::triangle
)
