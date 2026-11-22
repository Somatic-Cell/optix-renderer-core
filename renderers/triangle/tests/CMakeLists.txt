add_executable(triangle_config_io_test config_io_test.cpp)
renderer_configure_host(triangle_config_io_test)
target_link_libraries(triangle_config_io_test PRIVATE renderer::triangle_config renderer::config_io)
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
    renderer_add_application(triangle_session_test
        SOURCES session_test.cpp
        LINK renderer::triangle renderer::render_execution
    )
    add_test(NAME triangle_session COMMAND triangle_session_test)
    set_tests_properties(triangle_session PROPERTIES LABELS "gpu;triangle;session" TIMEOUT 120)
endif()
if(RENDERER_BUILD_VIEWER AND RENDERER_ENABLE_VIEWER_TESTS AND RENDERER_ENABLE_GPU_TESTS)
    renderer_add_application(triangle_viewer_test
        SOURCES viewer_test.cpp
        LINK renderer::triangle_viewer_application glfw OpenGL::GL fpng::fpng
    )
    add_test(NAME triangle_viewer_gpu_smoke COMMAND triangle_viewer_test)
    set_tests_properties(triangle_viewer_gpu_smoke PROPERTIES
        LABELS "viewer;opengl;gpu;optix" TIMEOUT 180 RUN_SERIAL TRUE)
endif()
