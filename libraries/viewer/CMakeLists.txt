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

if(WIN32)
    target_sources(renderer_viewer PRIVATE src/platform/window_theme_win32.cpp)
    target_link_libraries(renderer_viewer PRIVATE dwmapi user32)
else()
    target_sources(renderer_viewer PRIVATE src/platform/window_theme_other.cpp)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
