add_executable(render_cli main.cpp)
renderer_configure_host(render_cli)
target_link_libraries(render_cli PRIVATE
    renderer::triangle renderer::image_processing renderer::asset_io)

renderer_stage_optix_module(render_cli renderer_triangle_programs triangle programs.optixir)
target_compile_definitions(render_cli PRIVATE
    "RENDERER_TRIANGLE_MODULE_PATH=\"$<TARGET_FILE_DIR:render_cli>/modules/triangle/programs.optixir\"")
set_target_properties(render_cli PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:render_cli>")