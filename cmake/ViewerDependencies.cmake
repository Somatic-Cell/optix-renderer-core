include_guard(GLOBAL)

enable_language(C)
find_package(OpenGL REQUIRED)

renderer_require_submodule(ext/glfw CMakeLists.txt)
renderer_require_submodule(ext/imgui imgui.h)

# GLFW の設定
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "Build GLFW examples" FORCE)
set(GLFW_BUILD_TESTS    OFF CACHE BOOL "Build GLFW tests" FORCE)
set(GLFW_BUILD_DOCS     OFF CACHE BOOL "Build GLFW docs" FORCE)
set(GLFW_INSTALL        OFF CACHE BOOL "Install GLFW" FORCE)
set(GLFW_LIBRARY_TYPE   STATIC CACHE STRING "GLFW linkage for this application" FORCE)

set(USE_MSVC_RUNTIME_LIBRARY_DLL ON CACHE BOOL "Do not override project CRT in GLFW" FORCE)
add_subdirectory("${PROJECT_SOURCE_DIR}/ext/glfw"
    "${PROJECT_BINARY_DIR}/glfw-build" EXCLUDE_FROM_ALL)

set(_renderer_imgui_root "${PROJECT_SOURCE_DIR}/ext/imgui")
add_library(renderer_imgui STATIC
    "${_renderer_imgui_root}/imgui.cpp"
    "${_renderer_imgui_root}/imgui_draw.cpp"
    "${_renderer_imgui_root}/imgui_tables.cpp"
    "${_renderer_imgui_root}/imgui_widgets.cpp"
    "${_renderer_imgui_root}/backends/imgui_impl_glfw.cpp"
    "${_renderer_imgui_root}/backends/imgui_impl_opengl3.cpp"
)

add_library(imgui::imgui ALIAS renderer_imgui)
target_include_directories(renderer_imgui SYSTEM PUBLIC "${_renderer_imgui_root}")
target_compile_features(renderer_imgui PUBLIC cxx_std_11)
set_target_properties(renderer_imgui PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(renderer_imgui PRIVATE glfw OpenGL::GL ${CMAKE_DL_LIBS})
if(MSVC)
    target_compile_options(renderer_imgui PRIVATE /utf-8)
endif()
