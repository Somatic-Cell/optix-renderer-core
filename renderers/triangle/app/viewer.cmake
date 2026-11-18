add_executable(triangle_viewer viewer_main.cpp)
renderer_configure_host(triangle_viewer)
target_link_libraries(triangle_viewer PRIVATE renderer::triangle renderer::render_viewer)
set_target_properties(triangle_viewer PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_viewer")
renderer_stage_optix_module(triangle_viewer renderer_triangle_programs triangle programs.optixir)
renderer_stage_optix_module(triangle_viewer renderer_output_transform_kernel image_processing output_transform.fatbin)
target_compile_definitions(triangle_viewer PRIVATE
    "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:triangle_viewer>/modules\"")
