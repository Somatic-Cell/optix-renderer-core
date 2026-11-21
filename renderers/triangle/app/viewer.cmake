add_library(renderer_triangle_viewer_application STATIC viewer_application.cpp)
add_library(renderer::triangle_viewer_application ALIAS renderer_triangle_viewer_application)
renderer_configure_host(renderer_triangle_viewer_application)
target_include_directories(renderer_triangle_viewer_application PUBLIC "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(renderer_triangle_viewer_application
    PUBLIC renderer::render_viewer
    PRIVATE renderer::triangle
)

add_executable(triangle_viewer viewer_main.cpp)
renderer_configure_host(triangle_viewer)
target_link_libraries(triangle_viewer PRIVATE renderer::triangle_viewer_application)
set_target_properties(triangle_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_viewer")
renderer_stage_optix_module(triangle_viewer renderer_triangle_programs triangle programs.optixir)
renderer_stage_optix_module(triangle_viewer renderer_output_transform_kernel image_processing output_transform.fatbin)
target_compile_definitions(triangle_viewer PRIVATE
    "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:triangle_viewer>/modules\"")
