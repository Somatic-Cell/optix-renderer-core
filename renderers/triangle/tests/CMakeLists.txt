add_executable(triangle_config_io_test
    config_io_test.cpp ../app/render_config.cpp ../app/render_config_json.cpp)
renderer_configure_host(triangle_config_io_test)
target_include_directories(triangle_config_io_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app")
target_link_libraries(triangle_config_io_test PRIVATE renderer::config_io renderer::export_settings)
add_test(NAME triangle_config_io COMMAND triangle_config_io_test
    "${CMAKE_CURRENT_BINARY_DIR}/config_io_test_data/$<CONFIG>")
set_tests_properties(triangle_config_io PROPERTIES LABELS "cpu;config;triangle" TIMEOUT 30)
if(RENDERER_ENABLE_GPU_TESTS)
    configure_file(triangle_smoke.json "${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json" COPYONLY)
    add_test(NAME triangle_gpu_smoke COMMAND "${CMAKE_COMMAND}"
        "-DTRIANGLE_EXECUTABLE=$<TARGET_FILE:triangle_cli>"
        "-DTRIANGLE_SMOKE_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.cmake")
    set_tests_properties(triangle_gpu_smoke PROPERTIES LABELS "gpu;triangle" TIMEOUT 120)
    add_executable(triangle_session_test session_test.cpp)
    renderer_configure_host(triangle_session_test)
    target_link_libraries(triangle_session_test PRIVATE renderer::triangle renderer::render_execution)
    set_target_properties(triangle_session_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_session_test")
    renderer_stage_optix_module(triangle_session_test renderer_triangle_programs triangle programs.optixir)
    target_compile_definitions(triangle_session_test PRIVATE
        "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:triangle_session_test>/modules\"")
    add_test(NAME triangle_session COMMAND triangle_session_test)
    set_tests_properties(triangle_session PROPERTIES LABELS "gpu;triangle;session" TIMEOUT 120)
endif()
if(RENDERER_BUILD_VIEWER AND RENDERER_ENABLE_VIEWER_TESTS AND RENDERER_ENABLE_GPU_TESTS)
    add_executable(triangle_viewer_test viewer_test.cpp)
    renderer_configure_host(triangle_viewer_test)
    target_link_libraries(triangle_viewer_test PRIVATE renderer::triangle_viewer_application glfw OpenGL::GL)
    set_target_properties(triangle_viewer_test PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>/triangle_viewer_test")
    renderer_stage_optix_module(triangle_viewer_test renderer_triangle_programs triangle programs.optixir)
    renderer_stage_optix_module(triangle_viewer_test renderer_output_transform_kernel image_processing output_transform.fatbin)
    target_compile_definitions(triangle_viewer_test PRIVATE
        "RENDERER_MODULE_DIRECTORY=\"$<TARGET_FILE_DIR:triangle_viewer_test>/modules\"")
    add_test(NAME triangle_viewer_gpu_smoke COMMAND triangle_viewer_test)
    set_tests_properties(triangle_viewer_gpu_smoke PROPERTIES
        LABELS "viewer;opengl;gpu;optix" TIMEOUT 180 RUN_SERIAL TRUE)
endif()
