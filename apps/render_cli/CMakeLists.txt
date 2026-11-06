add_executable(render_cli main.cpp run.cpp triangle_check.cpp)
renderer_configure_host(render_cli)
target_link_libraries(render_cli PRIVATE
    renderer::triangle renderer::image_processing renderer::asset_io)

renderer_stage_optix_module(render_cli renderer_triangle_programs triangle programs.optixir)
renderer_stage_optix_module(render_cli renderer_output_transform_kernel image_processing output_transform.fatbin)
target_compile_definitions(render_cli PRIVATE
    "RENDERER_TRIANGLE_MODULE_PATH=\"$<TARGET_FILE_DIR:render_cli>/modules\"")
set_target_properties(render_cli PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:render_cli>"
    VS_DEBUGGER_COMMAND_ARGUMENTS "--config" \"${PROJECT_SOURCE_DIR}/configs/triangle.json\"")