# Included by this app's CMakeLists.txt only when RENDERER_BUILD_VIEWER is ON.
add_executable(triangle_viewer viewer_main.cpp)
renderer_configure_host(triangle_viewer)
target_link_libraries(triangle_viewer PRIVATE renderer::viewer)
set_target_properties(triangle_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_viewer"
)
