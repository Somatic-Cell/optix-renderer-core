add_library(renderer_viewer STATIC
    src/window.cpp
    src/workspace.cpp
)

add_library(renderer::viewer ALIAS renderer_viewer)
renderer_configure_host(renderer_viewer)
target_include_directories(renderer_viewer PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(renderer_viewer
    PUBLIC imgui::imgui
    PRIVATE glfw OpenGL::GL
)