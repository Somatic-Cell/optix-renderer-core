add_executable(triangle_cli
    main.cpp
    render_config.cpp
    run.cpp
    triangle_check.cpp
)

renderer_configure_host(triangle_cli)

target_link_libraries(triangle_cli
    PRIVATE
        renderer::triangle
        renderer::image_processing
        renderer::output
        renderer::asset_io
        nlohmann_json::nlohmann_json    
)

set_target_properties(triangle_cli PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_cli"
)

renderer_stage_optix_module(
    triangle_cli
    renderer_triangle_programs
    triangle
    programs.optixir
)

renderer_stage_optix_module(
    triangle_cli
    renderer_output_transform_kernel
    image_processing
    output_transform.fatbin
)

target_compile_definitions(triangle_cli
    PRIVATE
        "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:triangle_cli>/modules\""
)