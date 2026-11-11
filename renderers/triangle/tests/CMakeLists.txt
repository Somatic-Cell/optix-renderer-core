add_executable(triangle_config_io_test
    config_io_test.cpp
    ../app/render_config.cpp
    ../app/render_config_json.cpp
)
renderer_configure_host(triangle_config_io_test)
target_include_directories(triangle_config_io_test
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../app"
)
target_link_libraries(triangle_config_io_test
    PRIVATE renderer::config_io renderer::asset_io
)
add_test(
    NAME triangle_config_io
    COMMAND triangle_config_io_test
        "${CMAKE_CURRENT_BINARY_DIR}/config_io_test_data/$<CONFIG>"
)
set_tests_properties(triangle_config_io PROPERTIES LABELS "cpu;config;triangle" TIMEOUT 30)

if(RENDERER_ENABLE_GPU_TESTS)
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.json"
        "${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
        COPYONLY
    )

    add_test(
        NAME triangle_gpu_smoke
        COMMAND "${CMAKE_COMMAND}"
            "-DTRIANGLE_EXECUTABLE=$<TARGET_FILE:triangle_cli>"
            "-DTRIANGLE_SMOKE_CONFIG=${CMAKE_CURRENT_BINARY_DIR}/triangle_smoke.json"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/triangle_smoke.cmake"
    )
    set_tests_properties(triangle_gpu_smoke PROPERTIES
        LABELS "gpu;triangle"
        TIMEOUT 120
    )
endif()
