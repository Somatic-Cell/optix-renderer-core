add_library(renderer_viewer_ui STATIC
    src/window.cpp
    src/workspace.cpp
)

add_library(renderer::viewer ALIAS renderer_viewer_ui)

renderer_configure_host(renderer_viewer_ui)

target_include_directories(renderer_viewer_ui PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(renderer_viewer_ui
    PUBLIC imgui::imgui
    PRIVATE glfw OpenGL::GL
)

if(WIN32)
    target_sources(renderer_viewer_ui PRIVATE src/platform/window_theme_win32.cpp)
    target_link_libraries(renderer_viewer_ui PRIVATE dwmapi user32)
else()
    target_sources(renderer_viewer_ui PRIVATE src/platform/window_theme_other.cpp)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
